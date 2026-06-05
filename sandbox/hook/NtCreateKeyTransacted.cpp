#include "NtCreateKeyTransacted.hpp"

T_NtCreateKeyTransacted sys_NtCreateKeyTransacted = nullptr;

static NTSTATUS Hook_NtCreateKeyTransacted(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                           POBJECT_ATTRIBUTES ObjectAttributes, ULONG TitleIndex,
                                           PCUNICODE_STRING Class, ULONG CreateOptions, HANDLE TransactionHandle,
                                           PULONG Disposition)
{
    return sys_NtCreateKeyTransacted(KeyHandle, DesiredAccess, ObjectAttributes, TitleIndex, Class, CreateOptions,
                                     TransactionHandle, Disposition);
}

static void LoadNtCreateKeyTransacted()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtCreateKeyTransacted");
    sys_NtCreateKeyTransacted = reinterpret_cast<T_NtCreateKeyTransacted>(addr);
}

appbox::HookRecord appbox::HookNtCreateKeyTransacted = {
    "NtCreateKeyTransacted",
    LoadNtCreateKeyTransacted,
    (void**)&sys_NtCreateKeyTransacted,
    Hook_NtCreateKeyTransacted,
};
