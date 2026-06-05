#include "NtDeleteKey.hpp"

T_NtDeleteKey sys_NtDeleteKey = nullptr;

static NTSTATUS Hook_NtDeleteKey(HANDLE KeyHandle)
{
    return sys_NtDeleteKey(KeyHandle);
}

static void LoadNtDeleteKey()
{
    *appbox::HookNtDeleteKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtDeleteKey.name);
}

appbox::HookRecord appbox::HookNtDeleteKey = {
    "NtDeleteKey",
    LoadNtDeleteKey,
    (void**)&sys_NtDeleteKey,
    Hook_NtDeleteKey,
};
