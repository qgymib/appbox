#include "NtSetInformationKey.hpp"

T_NtSetInformationKey sys_NtSetInformationKey = nullptr;

static NTSTATUS Hook_NtSetInformationKey(HANDLE KeyHandle, KEY_SET_INFORMATION_CLASS KeySetInformationClass,
                                         PVOID KeySetInformation, ULONG KeySetInformationLength)
{
    return sys_NtSetInformationKey(KeyHandle, KeySetInformationClass, KeySetInformation, KeySetInformationLength);
}

static void LoadNtSetInformationKey()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtSetInformationKey.name);
    sys_NtSetInformationKey = reinterpret_cast<T_NtSetInformationKey>(addr);
}

appbox::HookRecord appbox::HookNtSetInformationKey = {
    "NtSetInformationKey",
    LoadNtSetInformationKey,
    (void**)&sys_NtSetInformationKey,
    Hook_NtSetInformationKey,
};
