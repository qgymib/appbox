#include "NtCreateKey.hpp"

T_NtCreateKey sys_NtCreateKey = nullptr;

static NTSTATUS Hook_NtCreateKey(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                 ULONG TitleIndex, PUNICODE_STRING Class, ULONG CreateOptions, PULONG Disposition)
{
    return sys_NtCreateKey(KeyHandle, DesiredAccess, ObjectAttributes, TitleIndex, Class, CreateOptions, Disposition);
}

static void LoadNtCreateKey()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtCreateKey");
    sys_NtCreateKey = reinterpret_cast<T_NtCreateKey>(addr);
}

appbox::HookRecord appbox::HookNtCreateKey = {
    "NtCreateKey",
    LoadNtCreateKey,
    (void**)&sys_NtCreateKey,
    Hook_NtCreateKey,
};
