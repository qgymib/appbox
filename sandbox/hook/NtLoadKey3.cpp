#include "NtLoadKey3.hpp"

T_NtLoadKey3 sys_NtLoadKey3 = nullptr;

static NTSTATUS Hook_NtLoadKey3(POBJECT_ATTRIBUTES TargetKey, POBJECT_ATTRIBUTES SourceFile, ULONG Flags,
                                PVOID ExtendedParameters, ULONG ExtendedParameterCount, ACCESS_MASK DesiredAccess,
                                PHANDLE RootHandle, PVOID Reserved)
{
    return sys_NtLoadKey3(TargetKey, SourceFile, Flags, ExtendedParameters, ExtendedParameterCount, DesiredAccess,
                          RootHandle, Reserved);
}

static void LoadNtLoadKey3()
{
    *appbox::HookNtLoadKey3.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtLoadKey3.name);
}

appbox::HookRecord appbox::HookNtLoadKey3 = {
    "NtLoadKey3",
    LoadNtLoadKey3,
    (void**)&sys_NtLoadKey3,
    Hook_NtLoadKey3,
};
