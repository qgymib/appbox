#include "NtFlushKey.hpp"

T_NtFlushKey sys_NtFlushKey = nullptr;

static NTSTATUS Hook_NtFlushKey(HANDLE KeyHandle)
{
    return sys_NtFlushKey(KeyHandle);
}

static void LoadNtFlushKey()
{
    *appbox::HookNtFlushKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtFlushKey.name);
}

appbox::HookRecord appbox::HookNtFlushKey = {
    "NtFlushKey",
    LoadNtFlushKey,
    (void**)&sys_NtFlushKey,
    Hook_NtFlushKey,
};
