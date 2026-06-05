#include "NtReplaceKey.hpp"

T_NtReplaceKey sys_NtReplaceKey = nullptr;

static NTSTATUS Hook_NtReplaceKey(POBJECT_ATTRIBUTES NewFile, HANDLE TargetHandle, POBJECT_ATTRIBUTES OldFile)
{
    return sys_NtReplaceKey(NewFile, TargetHandle, OldFile);
}

static void LoadNtReplaceKey()
{
    *appbox::HookNtReplaceKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtReplaceKey.name);
}

appbox::HookRecord appbox::HookNtReplaceKey = {
    "NtReplaceKey",
    LoadNtReplaceKey,
    (void**)&sys_NtReplaceKey,
    Hook_NtReplaceKey,
};
