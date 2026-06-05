#include "NtOpenKeyTransacted.hpp"

T_NtOpenKeyTransacted sys_NtOpenKeyTransacted = nullptr;

static NTSTATUS Hook_NtOpenKeyTransacted(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                         POBJECT_ATTRIBUTES ObjectAttributes, HANDLE TransactionHandle)
{
    return sys_NtOpenKeyTransacted(KeyHandle, DesiredAccess, ObjectAttributes, TransactionHandle);
}

static void LoadNtOpenKeyTransacted()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtOpenKeyTransacted");
    sys_NtOpenKeyTransacted = reinterpret_cast<T_NtOpenKeyTransacted>(addr);
}

appbox::HookRecord appbox::HookNtOpenKeyTransacted = {
    "NtOpenKeyTransacted",
    LoadNtOpenKeyTransacted,
    (void**)&sys_NtOpenKeyTransacted,
    Hook_NtOpenKeyTransacted,
};
