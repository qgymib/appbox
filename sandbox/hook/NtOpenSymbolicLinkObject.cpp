#include "NtOpenSymbolicLinkObject.hpp"

T_NtOpenSymbolicLinkObject sys_NtOpenSymbolicLinkObject = nullptr;

static NTSTATUS Hook_NtOpenSymbolicLinkObject(PHANDLE LinkHandle, ACCESS_MASK DesiredAccess,
                                              POBJECT_ATTRIBUTES ObjectAttributes)
{
    return sys_NtOpenSymbolicLinkObject(LinkHandle, DesiredAccess, ObjectAttributes);
}

static void LoadNtOpenSymbolicLinkObject()
{
    *appbox::HookNtOpenSymbolicLinkObject.ppPointer =
        GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtOpenSymbolicLinkObject.name);
}

appbox::HookRecord appbox::HookNtOpenSymbolicLinkObject = {
    "NtOpenSymbolicLinkObject",
    LoadNtOpenSymbolicLinkObject,
    (void**)&sys_NtOpenSymbolicLinkObject,
    Hook_NtOpenSymbolicLinkObject,
};
