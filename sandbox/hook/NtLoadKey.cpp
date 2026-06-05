#include "NtLoadKey.hpp"

T_NtLoadKey sys_NtLoadKey = nullptr;

static NTSTATUS Hook_NtLoadKey(POBJECT_ATTRIBUTES TargetKey, POBJECT_ATTRIBUTES SourceFile)
{
    return sys_NtLoadKey(TargetKey, SourceFile);
}

static void LoadNtLoadKey()
{
    *appbox::HookNtLoadKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtLoadKey.name);
}

appbox::HookRecord appbox::HookNtLoadKey = {
    "NtLoadKey",
    LoadNtLoadKey,
    (void**)&sys_NtLoadKey,
    Hook_NtLoadKey,
};
