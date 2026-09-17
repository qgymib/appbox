#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "registry/KeyGuard.hpp"
#include "utils/Log.hpp"
#include "NtQueryValueKey.hpp"

T_NtQueryValueKey sys_NtQueryValueKey = nullptr;

static nlohmann::json NtQueryValueKeyLogParam(HANDLE KeyHandle, PUNICODE_STRING ValueName,
                                              KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
                                              PVOID KeyValueInformation, ULONG Length, PULONG ResultLength)
{
    nlohmann::json json;
    json["KeyHandle"]                = appbox::PointerToString(KeyHandle);
    json["ValueName"]                = appbox::ToJson(ValueName);
    json["KeyValueInformationClass"] = KeyValueInformationClass;
    json["KeyValueInformation"]      = appbox::PointerToString(KeyValueInformation);
    json["Length"]                   = Length;
    json["ResultLength"]             = appbox::PointerToString(ResultLength);
    return json;
}

static appbox::LoggerF logger("NtQueryValueKey", NtQueryValueKeyLogParam);

/**
 * @brief Detour of NtQueryValueKey().
 *
 * A value query below a hive handle first runs against the hive layer. When
 * the value does not exist there, the query is replayed against the real key
 * which the view path addresses, so a shadow key no longer hides the values of
 * the real key. Buffer related results of the hive layer are returned as they
 * are: the hive holds the value, only the caller buffer is too small.
 *
 * Handles which do not point into the hive are forwarded unchanged.
 */
static NTSTATUS Hook_NtQueryValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName,
                                     KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass, PVOID KeyValueInformation,
                                     ULONG Length, PULONG ResultLength)
{
    logger.Log(KeyHandle, ValueName, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);

    std::wstring view_path;
    if (appbox::registry::Hive::MapHandleView(KeyHandle, view_path) != appbox::registry::HandleView::HiveHandle)
    {
        return sys_NtQueryValueKey(KeyHandle, ValueName, KeyValueInformationClass, KeyValueInformation, Length,
                                   ResultLength);
    }

    NTSTATUS st = sys_NtQueryValueKey(KeyHandle, ValueName, KeyValueInformationClass, KeyValueInformation, Length,
                                      ResultLength);
    if (st != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        return st;
    }

    /* Read through: the value is missing in the hive, try the real registry. */
    HANDLE real = nullptr;
    if (!NT_SUCCESS(appbox::registry::Hive::OpenRealKey(view_path, KEY_QUERY_VALUE, OBJ_CASE_INSENSITIVE, nullptr,
                                                        nullptr, &real)))
    {
        return st;
    }

    appbox::registry::KeyGuard real_guard(real);
    return sys_NtQueryValueKey(real, ValueName, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);
}

static void LoadNtQueryValueKey()
{
    sys_NtQueryValueKey =
        reinterpret_cast<T_NtQueryValueKey>(GetProcAddress(appbox::sys.h_ntdll, "NtQueryValueKey"));
}

appbox::HookRecord appbox::HookNtQueryValueKey = {
    "NtQueryValueKey",
    LoadNtQueryValueKey,
    (void**)&sys_NtQueryValueKey,
    Hook_NtQueryValueKey,
};
