#include "NtQueryKey.hpp"

T_NtQueryKey sys_NtQueryKey = nullptr;

static NTSTATUS Hook_NtQueryKey(HANDLE KeyHandle, KEY_INFORMATION_CLASS KeyInformationClass, PVOID KeyInformation,
                                ULONG Length, PULONG ResultLength)
{
    return sys_NtQueryKey(KeyHandle, KeyInformationClass, KeyInformation, Length, ResultLength);
}

static void LoadNtQueryKey()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtQueryKey");
    sys_NtQueryKey = reinterpret_cast<T_NtQueryKey>(addr);
}

appbox::HookRecord appbox::HookNtQueryKey = {
    "NtQueryKey",
    LoadNtQueryKey,
    (void**)&sys_NtQueryKey,
    Hook_NtQueryKey,
};
