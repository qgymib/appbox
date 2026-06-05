#include "NtEnumerateValueKey.hpp"

T_NtEnumerateValueKey sys_NtEnumerateValueKey = nullptr;

static NTSTATUS Hook_NtEnumerateValueKey(HANDLE KeyHandle, ULONG Index,
                                         KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
                                         PVOID KeyValueInformation, ULONG Length, PULONG ResultLength)
{
    return sys_NtEnumerateValueKey(KeyHandle, Index, KeyValueInformationClass, KeyValueInformation, Length,
                                   ResultLength);
}

static void LoadNtEnumerateValueKey()
{
    *appbox::HookNtEnumerateValueKey.ppPointer =
        GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtEnumerateValueKey.name);
}

appbox::HookRecord appbox::HookNtEnumerateValueKey = {
    "NtEnumerateValueKey",
    LoadNtEnumerateValueKey,
    (void**)&sys_NtEnumerateValueKey,
    Hook_NtEnumerateValueKey,
};
