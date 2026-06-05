#include "NtOpenKey.hpp"

T_NtOpenKey sys_NtOpenKey = nullptr;

static NTSTATUS Hook_NtOpenKey(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes)
{
    return sys_NtOpenKey(KeyHandle, DesiredAccess, ObjectAttributes);
}

static void LoadNtOpenKey()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtOpenKey");
    sys_NtOpenKey = reinterpret_cast<T_NtOpenKey>(addr);
}

appbox::HookRecord appbox::HookNtOpenKey = {
    "NtOpenKey",
    LoadNtOpenKey,
    (void**)&sys_NtOpenKey,
    Hook_NtOpenKey,
};
