#include "NtLoadKeyEx.hpp"

T_NtLoadKeyEx sys_NtLoadKeyEx = nullptr;

static NTSTATUS Hook_NtLoadKeyEx(POBJECT_ATTRIBUTES TargetKey, POBJECT_ATTRIBUTES SourceFile, ULONG Flags,
                                 HANDLE TrustClassKey, HANDLE Event, ACCESS_MASK DesiredAccess, PHANDLE RootHandle,
                                 PVOID Reserved)
{
    return sys_NtLoadKeyEx(TargetKey, SourceFile, Flags, TrustClassKey, Event, DesiredAccess, RootHandle, Reserved);
}

static void LoadNtLoadKeyEx()
{
    *appbox::HookNtLoadKeyEx.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtLoadKeyEx.name);
}

appbox::HookRecord appbox::HookNtLoadKeyEx = {
    "NtLoadKeyEx",
    LoadNtLoadKeyEx,
    (void**)&sys_NtLoadKeyEx,
    Hook_NtLoadKeyEx,
};
