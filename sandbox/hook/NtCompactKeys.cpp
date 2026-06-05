#include "NtCompactKeys.hpp"

T_NtCompactKeys sys_NtCompactKeys = nullptr;

static NTSTATUS Hook_NtCompactKeys(ULONG Count, HANDLE KeyArray[])
{
    return sys_NtCompactKeys(Count, KeyArray);
}

static void LoadNtCompactKeys()
{
    *appbox::HookNtCompactKeys.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtCompactKeys.name);
}

appbox::HookRecord appbox::HookNtCompactKeys = {
    "NtCompactKeys",
    LoadNtCompactKeys,
    (void**)&sys_NtCompactKeys,
    Hook_NtCompactKeys,
};
