#include "NtQueryMultipleValueKey.hpp"

T_NtQueryMultipleValueKey sys_NtQueryMultipleValueKey = nullptr;

static NTSTATUS Hook_NtQueryMultipleValueKey(HANDLE KeyHandle, PKEY_VALUE_ENTRY ValueEntries, ULONG EntryCount,
                                             PVOID ValueBuffer, PULONG BufferLength, PULONG RequiredBufferLength)
{
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
