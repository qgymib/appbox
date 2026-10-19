#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtDeleteValueKey.hpp"

T_NtDeleteValueKey sys_NtDeleteValueKey = nullptr;

static nlohmann::json NtDeleteValueKeyLogParam(HANDLE KeyHandle, PUNICODE_STRING ValueName)
{
    nlohmann::json json;
    json["KeyHandle"] = appbox::PointerToString(KeyHandle);
    json["ValueName"] = appbox::ToJson(ValueName);
    return json;
}

static appbox::LoggerF logger("NtDeleteValueKey", NtDeleteValueKeyLogParam);

/**
 * @brief Detour of NtDeleteValueKey().
 *
 * The delete is delegated to appbox::registry::Hive::DeleteIsolatedValue, which
 * resolves the layer of the value like a value write does: `Merge` deletes the
 * value of the real registry when the sandbox does not hold it, every other mode
 * removes the value of the hive and records a value which only the host holds as
 * deleted (a whiteout), so the read through of the value and the merged value
 * enumeration do not resurrect it.
 *
 * A handle of the host layer is delegated as well: it is the layer which holds a
 * value a merged key does not shadow. The isolation refuses the handle for every
 * other route, because a caller which opened the key without the right to write
 * must not gain it.
 *
 * A call whose value name cannot be read is forwarded unchanged, because a
 * hook must never read outside the memory of the caller.
 *
 * Handles outside the root keys of the view are forwarded unchanged.
 */
static NTSTATUS NTAPI Hook_NtDeleteValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName)
{
    logger.Log(KeyHandle, ValueName);

    std::wstring                       view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (view == appbox::registry::HandleView::NotIsolated)
    {
        return sys_NtDeleteValueKey(KeyHandle, ValueName);
    }

    std::wstring relative;
    std::wstring value_name;
    if (!appbox::registry::Hive::HiveRelativePath(view_path, relative) ||
        !appbox::registry::ReadValueName(ValueName, value_name))
    {
        return sys_NtDeleteValueKey(KeyHandle, ValueName);
    }

    return appbox::registry::Hive::DeleteIsolatedValue(view, KeyHandle, view_path, relative, value_name);
}

static void LoadNtDeleteValueKey()
{
    sys_NtDeleteValueKey =
        reinterpret_cast<T_NtDeleteValueKey>(GetProcAddress(appbox::sys.h_ntdll, "NtDeleteValueKey"));
}

appbox::HookRecord appbox::HookNtDeleteValueKey = {
    "NtDeleteValueKey",
    LoadNtDeleteValueKey,
    (void**)&sys_NtDeleteValueKey,
    Hook_NtDeleteValueKey,
};
