#include "NtOpenKeyEx.hpp"

T_NtOpenKeyEx sys_NtOpenKeyEx = nullptr;

static NTSTATUS Hook_NtOpenKeyEx(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                 ULONG OpenOptions)
{
    return sys_NtOpenKeyEx(KeyHandle, DesiredAccess, ObjectAttributes, OpenOptions);
}

static void LoadNtOpenKeyEx()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtOpenKeyEx");
    sys_NtOpenKeyEx = reinterpret_cast<T_NtOpenKeyEx>(addr);
}

appbox::HookRecord appbox::HookNtOpenKeyEx = {
    "NtOpenKeyEx",
    LoadNtOpenKeyEx,
    (void**)&sys_NtOpenKeyEx,
    Hook_NtOpenKeyEx,
};
