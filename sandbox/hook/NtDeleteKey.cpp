#include "WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtDeleteKey.hpp"

T_NtDeleteKey sys_NtDeleteKey = nullptr;

static nlohmann::json NtDeleteKeyLogParam(HANDLE KeyHandle)
{
    nlohmann::json json;
    json["KeyHandle"] = appbox::PointerToString(KeyHandle);
    return json;
}

static appbox::LoggerF logger("NtDeleteKey", NtDeleteKeyLogParam);

/**
 * @brief Detour of NtDeleteKey().
 *
 * The delete is delegated to appbox::registry::Hive::DeleteIsolatedKey, which
 * resolves the layer of the key: `Merge` deletes the key of the real registry
 * when the sandbox does not hold it, every other mode removes the key of the
 * hive and records a key which only the host holds as deleted (a whiteout), so
 * the read through does not resurrect it.
 *
 * A handle of the host layer is delegated as well, because it is the layer which
 * holds a key a merged mode does not shadow. The isolation refuses the handle
 * for every other mode: a caller which opened the key without the right to
 * delete must not gain it, and the host layer must only be modified by `Merge`.
 *
 * Paths outside the root keys of the view are forwarded unchanged.
 */
static NTSTATUS NTAPI Hook_NtDeleteKey(HANDLE KeyHandle)
{
    logger.Log(KeyHandle);

    std::wstring                       view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (view == appbox::registry::HandleView::NotIsolated)
    {
        return sys_NtDeleteKey(KeyHandle);
    }

    std::wstring relative;
    if (!appbox::registry::Hive::HiveRelativePath(view_path, relative))
    {
        return sys_NtDeleteKey(KeyHandle);
    }

    return appbox::registry::Hive::DeleteIsolatedKey(view, KeyHandle, view_path, relative);
}

static void LoadNtDeleteKey()
{
    sys_NtDeleteKey = reinterpret_cast<T_NtDeleteKey>(GetProcAddress(appbox::sys.h_ntdll, "NtDeleteKey"));
}

appbox::HookRecord appbox::HookNtDeleteKey = {
    "NtDeleteKey",
    LoadNtDeleteKey,
    (void**)&sys_NtDeleteKey,
    Hook_NtDeleteKey,
};
