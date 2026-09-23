#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtSaveKeyEx.hpp"

T_NtSaveKeyEx sys_NtSaveKeyEx = nullptr;

static nlohmann::json NtSaveKeyExLogParam(HANDLE KeyHandle, HANDLE FileHandle, ULONG Format)
{
    nlohmann::json json;
    json["KeyHandle"]  = appbox::PointerToString(KeyHandle);
    json["FileHandle"] = appbox::PointerToString(FileHandle);
    json["Format"]     = Format;
    return json;
}

static appbox::LoggerF logger("NtSaveKeyEx", NtSaveKeyExLogParam);

/**
 * @brief Detour of NtSaveKeyEx().
 *
 * Same policy as NtSaveKey(): the saved file holds the merged view of the key,
 * and the format of the caller is forwarded to the save of the snapshot (see
 * appbox::registry::Hive::SaveIsolatedKey).
 *
 * Paths outside the root keys of the view are forwarded unchanged.
 */
static NTSTATUS Hook_NtSaveKeyEx(HANDLE KeyHandle, HANDLE FileHandle, ULONG Format)
{
    logger.Log(KeyHandle, FileHandle, Format);

    std::wstring view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (view == appbox::registry::HandleView::NotIsolated)
    {
        return sys_NtSaveKeyEx(KeyHandle, FileHandle, Format);
    }

    std::wstring relative;
    if (!appbox::registry::Hive::HiveRelativePath(view_path, relative))
    {
        return sys_NtSaveKeyEx(KeyHandle, FileHandle, Format);
    }

    return appbox::registry::Hive::SaveIsolatedKey(
        view == appbox::registry::HandleView::HiveHandle ? KeyHandle : nullptr, view_path, relative, FileHandle,
        Format, true);
}

static void LoadNtSaveKeyEx()
{
    sys_NtSaveKeyEx = reinterpret_cast<T_NtSaveKeyEx>(GetProcAddress(appbox::sys.h_ntdll, "NtSaveKeyEx"));
}

appbox::HookRecord appbox::HookNtSaveKeyEx = {
    "NtSaveKeyEx",
    LoadNtSaveKeyEx,
    (void**)&sys_NtSaveKeyEx,
    Hook_NtSaveKeyEx,
};
