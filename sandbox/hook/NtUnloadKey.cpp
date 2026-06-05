#include "NtUnloadKey.hpp"

T_NtUnloadKey sys_NtUnloadKey = nullptr;

static NTSTATUS Hook_NtUnloadKey(POBJECT_ATTRIBUTES TargetKey)
{
    return sys_NtUnloadKey(TargetKey);
}

static void LoadNtUnloadKey()
{
    *appbox::HookNtUnloadKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtUnloadKey.name);
}

appbox::HookRecord appbox::HookNtUnloadKey = {
    "NtUnloadKey",
    LoadNtUnloadKey,
    (void**)&sys_NtUnloadKey,
    Hook_NtUnloadKey,
};
