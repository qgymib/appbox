#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtSaveKey.hpp"

T_NtSaveKey sys_NtSaveKey = nullptr;

static nlohmann::json NtSaveKeyLogParam(HANDLE KeyHandle, HANDLE FileHandle)
{
    nlohmann::json json;
    json["KeyHandle"]  = appbox::PointerToString(KeyHandle);
    json["FileHandle"] = appbox::PointerToString(FileHandle);
    return json;
}

static appbox::LoggerF logger("NtSaveKey", NtSaveKeyLogParam);

/**
 * @brief Detour of NtSaveKey().
 *
 * The saved file holds the merged view of the key: the entries of the hive
 * layer plus the host entries which the isolation mode keeps visible. The
 * snapshot is built by appbox::registry::Hive::SaveIsolatedKey, which saves a
 * key whose real layer contributes nothing directly and copies the merged
 * content into a temporary hive otherwise, so an entry which a mode or a
 * whiteout hides never reaches the file.
 *
 * Paths outside the root keys of the view are forwarded unchanged.
 */
static NTSTATUS Hook_NtSaveKey(HANDLE KeyHandle, HANDLE FileHandle)
{
    logger.Log(KeyHandle, FileHandle);

    std::wstring view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (view == appbox::registry::HandleView::NotIsolated)
    {
        return sys_NtSaveKey(KeyHandle, FileHandle);
    }

    std::wstring relative;
    if (!appbox::registry::Hive::HiveRelativePath(view_path, relative))
    {
        return sys_NtSaveKey(KeyHandle, FileHandle);
    }

    return appbox::registry::Hive::SaveIsolatedKey(
        view == appbox::registry::HandleView::HiveHandle ? KeyHandle : nullptr, view_path, relative, FileHandle,
        REG_STANDARD_FORMAT, false);
}

static void LoadNtSaveKey()
{
    sys_NtSaveKey = reinterpret_cast<T_NtSaveKey>(GetProcAddress(appbox::sys.h_ntdll, "NtSaveKey"));
}

appbox::HookRecord appbox::HookNtSaveKey = {
    "NtSaveKey",
    LoadNtSaveKey,
    (void**)&sys_NtSaveKey,
    Hook_NtSaveKey,
};
