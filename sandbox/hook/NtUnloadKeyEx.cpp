#include "NtUnloadKeyEx.hpp"

T_NtUnloadKeyEx sys_NtUnloadKeyEx = nullptr;

static NTSTATUS Hook_NtUnloadKeyEx(POBJECT_ATTRIBUTES TargetKey, HANDLE Event)
{
    return sys_NtUnloadKeyEx(TargetKey, Event);
}

static void LoadNtUnloadKeyEx()
{
    *appbox::HookNtUnloadKeyEx.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtUnloadKeyEx.name);
}

appbox::HookRecord appbox::HookNtUnloadKeyEx = {
    "NtUnloadKeyEx",
    LoadNtUnloadKeyEx,
    (void**)&sys_NtUnloadKeyEx,
    Hook_NtUnloadKeyEx,
};
