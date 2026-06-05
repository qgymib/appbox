#include "NtSaveKey.hpp"

T_NtSaveKey sys_NtSaveKey = nullptr;

static NTSTATUS Hook_NtSaveKey(HANDLE KeyHandle, HANDLE FileHandle)
{
    return sys_NtSaveKey(KeyHandle, FileHandle);
}

static void LoadNtSaveKey()
{
    *appbox::HookNtSaveKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtSaveKey.name);
}

appbox::HookRecord appbox::HookNtSaveKey = {
    "NtSaveKey",
    LoadNtSaveKey,
    (void**)&sys_NtSaveKey,
    Hook_NtSaveKey,
};
