#include "NtCompressKey.hpp"

T_NtCompressKey sys_NtCompressKey = nullptr;

static NTSTATUS Hook_NtCompressKey(HANDLE KeyHandle)
{
    return sys_NtCompressKey(KeyHandle);
}

static void LoadNtCompressKey()
{
    *appbox::HookNtCompressKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtCompressKey.name);
}

appbox::HookRecord appbox::HookNtCompressKey = {
    "NtCompressKey",
    LoadNtCompressKey,
    (void**)&sys_NtCompressKey,
    Hook_NtCompressKey,
};
