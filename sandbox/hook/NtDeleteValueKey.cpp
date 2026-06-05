#include "NtDeleteValueKey.hpp"

T_NtDeleteValueKey sys_NtDeleteValueKey = nullptr;

static NTSTATUS Hook_NtDeleteValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName)
{
    return sys_NtDeleteValueKey(KeyHandle, ValueName);
}

static void LoadNtDeleteValueKey()
{
    *appbox::HookNtDeleteValueKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtDeleteValueKey.name);
}

appbox::HookRecord appbox::HookNtDeleteValueKey = {
    "NtDeleteValueKey",
    LoadNtDeleteValueKey,
    (void**)&sys_NtDeleteValueKey,
    Hook_NtDeleteValueKey,
};
