#include "utils/WinAPI.h" /* Must be first include file */
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
 * The delete runs against the merged view and never against the real registry.
 * A handle of the hive is delegated to
 * appbox::registry::Hive::DeleteIsolatedKey: the key of the hive is removed
 * and a key which only the host holds is recorded as deleted (a whiteout), so
 * the read through does not resurrect it.
 *
 * A handle of the host layer is refused: it is a read through handle which the
 * caller opened without the right to delete (an open which asks for `DELETE`
 * is copied up into the hive), so the real call would report an access denial
 * as well — and the isolation must never let a delete reach the real registry.
 *
 * Paths outside the root keys of the view are forwarded unchanged.
 */
static NTSTATUS Hook_NtDeleteKey(HANDLE KeyHandle)
{
    logger.Log(KeyHandle);

    std::wstring view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (view == appbox::registry::HandleView::NotIsolated)
    {
        return sys_NtDeleteKey(KeyHandle);
    }

    if (view == appbox::registry::HandleView::RealHandle)
    {
        return STATUS_ACCESS_DENIED;
    }

    std::wstring relative;
    if (!appbox::registry::Hive::HiveRelativePath(view_path, relative))
    {
        return sys_NtDeleteKey(KeyHandle);
    }

    return appbox::registry::Hive::DeleteIsolatedKey(KeyHandle, view_path, relative);
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
