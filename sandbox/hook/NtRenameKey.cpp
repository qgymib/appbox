#include "NtRenameKey.hpp"

T_NtRenameKey sys_NtRenameKey = nullptr;

static NTSTATUS Hook_NtRenameKey(HANDLE KeyHandle, PUNICODE_STRING NewName)
{
    return sys_NtRenameKey(KeyHandle, NewName);
}

static void LoadNtRenameKey()
{
    *appbox::HookNtRenameKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtRenameKey.name);
}

appbox::HookRecord appbox::HookNtRenameKey = {
    "NtRenameKey",
    LoadNtRenameKey,
    (void**)&sys_NtRenameKey,
    Hook_NtRenameKey,
};
