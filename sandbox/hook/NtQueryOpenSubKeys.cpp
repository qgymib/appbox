#include "NtQueryOpenSubKeys.hpp"

T_NtQueryOpenSubKeys sys_NtQueryOpenSubKeys = nullptr;

static NTSTATUS Hook_NtQueryOpenSubKeys(POBJECT_ATTRIBUTES TargetKey, PULONG HandleCount)
{
    return sys_NtQueryOpenSubKeys(TargetKey, HandleCount);
}

static void LoadNtQueryOpenSubKeys()
{
    *appbox::HookNtQueryOpenSubKeys.ppPointer =
        GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtQueryOpenSubKeys.name);
}

appbox::HookRecord appbox::HookNtQueryOpenSubKeys = {
    "NtQueryOpenSubKeys",
    LoadNtQueryOpenSubKeys,
    (void**)&sys_NtQueryOpenSubKeys,
    Hook_NtQueryOpenSubKeys,
};
