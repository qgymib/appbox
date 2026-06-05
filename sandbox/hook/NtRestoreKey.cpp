#include "NtRestoreKey.hpp"

T_NtRestoreKey sys_NtRestoreKey = nullptr;

static NTSTATUS Hook_NtRestoreKey(HANDLE KeyHandle, HANDLE FileHandle, ULONG Flags)
{
    return sys_NtRestoreKey(KeyHandle, FileHandle, Flags);
}

static void LoadNtRestoreKey()
{
    *appbox::HookNtRestoreKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtRestoreKey.name);
}

appbox::HookRecord appbox::HookNtRestoreKey = {
    "NtRestoreKey",
    LoadNtRestoreKey,
    (void**)&sys_NtRestoreKey,
    Hook_NtRestoreKey,
};
