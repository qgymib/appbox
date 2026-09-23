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
 * The delete runs against the merged view and never against the real registry.
 * A handle of the hive is delegated to
 * appbox::registry::Hive::DeleteIsolatedValue: the value of the hive is
 * removed and a value which only the host holds is recorded as deleted (a
 * whiteout), so the read through of the value and the merged value enumeration
 * do not resurrect it.
 *
 * A handle of the host layer is refused, because it is a read through handle
 * which the caller opened without the right to write (an open which asks for
 * `KEY_SET_VALUE` is copied up into the hive) and because the isolation must
 * never let a delete reach the real registry.
 *
 * A call whose value name cannot be read is forwarded unchanged, because a
 * hook must never read outside the memory of the caller.
 *
 * Handles outside the root keys of the view are forwarded unchanged.
 */
static NTSTATUS Hook_NtDeleteValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName)
{
    logger.Log(KeyHandle, ValueName);

    std::wstring view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (view == appbox::registry::HandleView::NotIsolated)
    {
        return sys_NtDeleteValueKey(KeyHandle, ValueName);
    }

    if (view == appbox::registry::HandleView::RealHandle)
    {
        return STATUS_ACCESS_DENIED;
    }

    std::wstring relative;
    std::wstring value_name;
    if (!appbox::registry::Hive::HiveRelativePath(view_path, relative) ||
        !appbox::registry::ReadValueName(ValueName, value_name))
    {
        return sys_NtDeleteValueKey(KeyHandle, ValueName);
    }

    return appbox::registry::Hive::DeleteIsolatedValue(KeyHandle, view_path, relative, value_name);
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
