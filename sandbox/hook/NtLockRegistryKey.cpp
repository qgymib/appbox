#include "NtLockRegistryKey.hpp"

T_NtLockRegistryKey sys_NtLockRegistryKey = nullptr;

static NTSTATUS Hook_NtLockRegistryKey(HANDLE KeyHandle)
{
    return sys_NtLockRegistryKey(KeyHandle);
}

static void LoadNtLockRegistryKey()
{
    *appbox::HookNtLockRegistryKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtLockRegistryKey.name);
}

appbox::HookRecord appbox::HookNtLockRegistryKey = {
    "NtLockRegistryKey",
    LoadNtLockRegistryKey,
    (void**)&sys_NtLockRegistryKey,
    Hook_NtLockRegistryKey,
};
