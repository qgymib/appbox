#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtQueryMultipleValueKey.hpp"

T_NtQueryMultipleValueKey sys_NtQueryMultipleValueKey = nullptr;

static nlohmann::json NtQueryMultipleValueKeyLogParam(HANDLE KeyHandle, PKEY_VALUE_ENTRY ValueEntries, ULONG EntryCount,
                                                      PVOID ValueBuffer, PULONG BufferLength,
                                                      PULONG RequiredBufferLength)
{
    nlohmann::json json;
    json["KeyHandle"]            = appbox::PointerToString(KeyHandle);
    json["ValueEntries"]         = appbox::PointerToString(ValueEntries);
    json["EntryCount"]           = EntryCount;
    json["ValueBuffer"]          = appbox::PointerToString(ValueBuffer);
    json["BufferLength"]         = appbox::PointerToString(BufferLength);
    json["RequiredBufferLength"] = appbox::PointerToString(RequiredBufferLength);
    return json;
}

static appbox::LoggerF logger("NtQueryMultipleValueKey", NtQueryMultipleValueKeyLogParam);

/**
 * @brief Detour of NtQueryMultipleValueKey().
 *
 * A batch query below a hive handle follows the same read through rule as
 * NtQueryValueKey: every entry is answered by the layer which holds it, so a
 * value which only exists in the real registry stays readable and a value
 * which the isolation hides or which the sandbox deleted (a whiteout) is
 * reported as missing. The whole policy lives in
 * appbox::registry::Hive::QueryMultipleValues.
 *
 * Handles which do not point into the hive are forwarded unchanged.
 */
static NTSTATUS Hook_NtQueryMultipleValueKey(HANDLE KeyHandle, PKEY_VALUE_ENTRY ValueEntries, ULONG EntryCount,
                                             PVOID ValueBuffer, PULONG BufferLength, PULONG RequiredBufferLength)
{
    logger.Log(KeyHandle, ValueEntries, EntryCount, ValueBuffer, BufferLength, RequiredBufferLength);

    std::wstring view_path;
    if (appbox::registry::Hive::MapHandleView(KeyHandle, view_path) != appbox::registry::HandleView::HiveHandle)
    {
        return sys_NtQueryMultipleValueKey(KeyHandle, ValueEntries, EntryCount, ValueBuffer, BufferLength,
                                           RequiredBufferLength);
    }

    std::wstring relative;
    if (!appbox::registry::Hive::HiveRelativePath(view_path, relative))
    {
        return sys_NtQueryMultipleValueKey(KeyHandle, ValueEntries, EntryCount, ValueBuffer, BufferLength,
                                           RequiredBufferLength);
    }

    return appbox::registry::Hive::QueryMultipleValues(KeyHandle, view_path, relative, ValueEntries, EntryCount,
                                                       ValueBuffer, BufferLength, RequiredBufferLength);
}

static void LoadNtQueryMultipleValueKey()
{
    sys_NtQueryMultipleValueKey =
        reinterpret_cast<T_NtQueryMultipleValueKey>(GetProcAddress(appbox::sys.h_ntdll, "NtQueryMultipleValueKey"));
}

appbox::HookRecord appbox::HookNtQueryMultipleValueKey = {
    "NtQueryMultipleValueKey",
    LoadNtQueryMultipleValueKey,
    (void**)&sys_NtQueryMultipleValueKey,
    Hook_NtQueryMultipleValueKey,
};
