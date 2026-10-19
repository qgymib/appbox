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
 * The rename is delegated to appbox::registry::Hive::RenameIsolatedKey, which
 * resolves the layer of the key: `Merge` renames the key of the real registry
 * when the sandbox does not hold it, every other route renames the key of the
 * hive and records the visible host key of the old name as deleted (a whiteout),
 * so the merged view reports the new name alone instead of both of them.
 *
 * A handle of the host layer is delegated as well, because it is the layer which
 * holds a key a merged mode does not shadow. The isolation refuses the handle for
 * every other route: a caller which opened the key without the right to rename
 * must not gain it, and a rename which entered the sandbox from outside must not
 * carry the right of the host layer.
 *
 * A call whose new name cannot be read is refused instead of being forwarded: a
 * forwarded rename of a key of the hive would leave the host key of the old name
 * in the view. The kernel reports `STATUS_INVALID_PARAMETER` for a name which is
 * empty or which carries a separator, which is the answer of an unusable name as
 * well.
 *
 * Paths outside the root keys of the view are forwarded unchanged.
 */
static NTSTATUS NTAPI Hook_NtRenameKey(HANDLE KeyHandle, PUNICODE_STRING NewName)
{
    logger.Log(KeyHandle, NewName);

    std::wstring                       view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (view == appbox::registry::HandleView::NotIsolated)
    {
        return sys_NtRenameKey(KeyHandle, NewName);
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

    return appbox::registry::Hive::RenameIsolatedKey(view, KeyHandle, view_path, relative, new_name);
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
