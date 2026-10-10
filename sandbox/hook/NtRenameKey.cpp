#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtRenameKey.hpp"

T_NtRenameKey sys_NtRenameKey = nullptr;

static nlohmann::json NtRenameKeyLogParam(HANDLE KeyHandle, PUNICODE_STRING NewName)
{
    nlohmann::json json;
    json["KeyHandle"] = appbox::PointerToString(KeyHandle);
    json["NewName"] = appbox::ToJson(NewName);
    return json;
}

static appbox::LoggerF logger("NtRenameKey", NtRenameKeyLogParam);

/**
 * @brief Detour of NtRenameKey().
 *
 * The rename runs against the merged view and never against the real registry.
 * A handle of the hive is delegated to
 * appbox::registry::Hive::RenameIsolatedKey: the key of the hive is renamed
 * and the visible host key of the old name is recorded as deleted (a whiteout),
 * so the merged view reports the new name alone instead of both of them.
 *
 * A handle of the host layer is refused: it is a read through handle which the
 * caller opened without the right to rename (an open which asks for a write
 * right is copied up into the hive), and the isolation must never let a rename
 * reach the real registry — a handle which entered the sandbox from outside
 * could otherwise carry the right of the host layer.
 *
 * A call whose new name cannot be read is refused as well instead of being
 * forwarded: a forwarded rename of a key of the hive would leave the host key
 * of the old name in the view. The kernel reports `STATUS_INVALID_PARAMETER`
 * for a name which is empty or which carries a separator, which is the answer
 * of an unusable name as well.
 *
 * Paths outside the root keys of the view are forwarded unchanged.
 */
static NTSTATUS Hook_NtRenameKey(HANDLE KeyHandle, PUNICODE_STRING NewName)
{
    logger.Log(KeyHandle, NewName);

    std::wstring                       view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (view == appbox::registry::HandleView::NotIsolated)
    {
        return sys_NtRenameKey(KeyHandle, NewName);
    }

    if (view == appbox::registry::HandleView::RealHandle)
    {
        return STATUS_ACCESS_DENIED;
    }

    std::wstring relative;
    if (!appbox::registry::Hive::HiveRelativePath(view_path, relative))
    {
        return sys_NtRenameKey(KeyHandle, NewName);
    }

    std::wstring new_name;
    if (!appbox::registry::ReadValueName(NewName, new_name) || new_name.empty())
    {
        return STATUS_INVALID_PARAMETER;
    }

    return appbox::registry::Hive::RenameIsolatedKey(KeyHandle, view_path, relative, new_name);
}

static void LoadNtRenameKey()
{
    sys_NtRenameKey = reinterpret_cast<T_NtRenameKey>(GetProcAddress(appbox::sys.h_ntdll, "NtRenameKey"));
}

appbox::HookRecord appbox::HookNtRenameKey = {
    "NtRenameKey",
    LoadNtRenameKey,
    (void**)&sys_NtRenameKey,
    Hook_NtRenameKey,
};
