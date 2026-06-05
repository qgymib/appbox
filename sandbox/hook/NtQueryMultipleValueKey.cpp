#include "utils/Log.hpp"
#include "NtQueryMultipleValueKey.hpp"

T_NtQueryMultipleValueKey sys_NtQueryMultipleValueKey = nullptr;

static nlohmann::json KeyValueEntryToJson(PKEY_VALUE_ENTRY ValueEntries)
{
    nlohmann::json j;
    j["ValueName"] = appbox::ToJson(ValueEntries->ValueName);
    j["DataLength"] = ValueEntries->DataLength;
    j["DataOffset"] = ValueEntries->DataOffset;
    j["Type"] = ValueEntries->Type;
    return j;
}

static nlohmann::json NtQueryMultipleValueKeyLogParam(HANDLE KeyHandle, PKEY_VALUE_ENTRY ValueEntries, ULONG EntryCount,
                                                      PVOID ValueBuffer, PULONG BufferLength,
                                                      PULONG RequiredBufferLength)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["ValueEntries"] = KeyValueEntryToJson(ValueEntries);
    param["EntryCount"] = EntryCount;
    param["ValueBuffer"] = appbox::PointerToString(ValueBuffer);
    param["BufferLength"] = appbox::PointerToString(BufferLength);
    param["RequiredBufferLength"] = appbox::PointerToString(RequiredBufferLength);
    return param;
}

static appbox::LoggerF logger("NtQueryMultipleValueKey", NtQueryMultipleValueKeyLogParam);

static NTSTATUS Hook_NtQueryMultipleValueKey(HANDLE KeyHandle, PKEY_VALUE_ENTRY ValueEntries, ULONG EntryCount,
                                             PVOID ValueBuffer, PULONG BufferLength, PULONG RequiredBufferLength)
{
    logger.Log(KeyHandle, ValueEntries, EntryCount, ValueBuffer, BufferLength, RequiredBufferLength);
    return sys_NtQueryMultipleValueKey(KeyHandle, ValueEntries, EntryCount, ValueBuffer, BufferLength,
                                       RequiredBufferLength);
}

static void LoadNtQueryMultipleValueKey()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtQueryMultipleValueKey");
    sys_NtQueryMultipleValueKey = reinterpret_cast<T_NtQueryMultipleValueKey>(addr);
}

appbox::HookRecord appbox::HookNtQueryMultipleValueKey = {
    "NtQueryMultipleValueKey",
    LoadNtQueryMultipleValueKey,
    (void**)&sys_NtQueryMultipleValueKey,
    Hook_NtQueryMultipleValueKey,
};
