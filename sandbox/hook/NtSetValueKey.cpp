#include "NtSetValueKey.hpp"

T_NtSetValueKey sys_NtSetValueKey = nullptr;

static NTSTATUS Hook_NtSetValueKey(HANDLE KeyHandle, ULONG Index, KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
                                   PVOID KeyValueInformation, ULONG Length, PULONG ResultLength)
{
    return sys_NtSetValueKey(KeyHandle, Index, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);
}

static void LoadNtSetValueKey()
{
    *appbox::HookNtSetValueKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtSetValueKey.name);
}

appbox::HookRecord appbox::HookNtSetValueKey = {
    "NtSetValueKey",
    LoadNtSetValueKey,
    (void**)&sys_NtSetValueKey,
    Hook_NtSetValueKey,
};
