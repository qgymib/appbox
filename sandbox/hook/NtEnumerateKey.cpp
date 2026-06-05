#include "NtEnumerateKey.hpp"

T_NtEnumerateKey sys_NtEnumerateKey = nullptr;

static NTSTATUS Hook_NtEnumerateKey(HANDLE KeyHandle, ULONG Index, KEY_INFORMATION_CLASS KeyInformationClass,
                                    PVOID KeyInformation, ULONG Length, PULONG ResultLength)
{
    return sys_NtEnumerateKey(KeyHandle, Index, KeyInformationClass, KeyInformation, Length, ResultLength);
}

static void LoadNtEnumerateKey()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtEnumerateKey");
    sys_NtEnumerateKey = reinterpret_cast<T_NtEnumerateKey>(addr);
}

appbox::HookRecord appbox::HookNtEnumerateKey = {
    "NtEnumerateKey",
    LoadNtEnumerateKey,
    (void**)&sys_NtEnumerateKey,
    Hook_NtEnumerateKey,
};
