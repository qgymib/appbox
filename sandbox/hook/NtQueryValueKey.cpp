#include "NtQueryValueKey.hpp"

T_NtQueryValueKey sys_NtQueryValueKey = nullptr;

static NTSTATUS Hook_NtQueryValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName,
                                     KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass, PVOID KeyValueInformation,
                                     ULONG Length, PULONG ResultLength)
{
    return sys_NtQueryValueKey(KeyHandle, ValueName, KeyValueInformationClass, KeyValueInformation, Length,
                               ResultLength);
}

static void LoadNtQueryValueKey()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtQueryValueKey");
    sys_NtQueryValueKey = reinterpret_cast<T_NtQueryValueKey>(addr);
}

appbox::HookRecord appbox::HookNtQueryValueKey = {
    "NtQueryValueKey",
    LoadNtQueryValueKey,
    (void**)&sys_NtQueryValueKey,
    Hook_NtQueryValueKey,
};
