#include "NtOpenKeyTransactedEx.hpp"

T_NtOpenKeyTransactedEx sys_NtOpenKeyTransactedEx = nullptr;

static NTSTATUS Hook_NtOpenKeyTransactedEx(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                           POBJECT_ATTRIBUTES ObjectAttributes, ULONG OpenOptions,
                                           HANDLE TransactionHandle)
{
    return sys_NtOpenKeyTransactedEx(KeyHandle, DesiredAccess, ObjectAttributes, OpenOptions, TransactionHandle);
}

static void LoadNtOpenKeyTransactedEx()
{
    *appbox::HookNtOpenKeyTransactedEx.ppPointer =
        GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtOpenKeyTransactedEx.name);
}

appbox::HookRecord appbox::HookNtOpenKeyTransactedEx = {
    "NtOpenKeyTransactedEx",
    LoadNtOpenKeyTransactedEx,
    (void**)&sys_NtOpenKeyTransactedEx,
    Hook_NtOpenKeyTransactedEx,
};
