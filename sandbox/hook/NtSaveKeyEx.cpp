#include "NtSaveKeyEx.hpp"

T_NtSaveKeyEx sys_NtSaveKeyEx = nullptr;

static NTSTATUS Hook_NtSaveKeyEx(HANDLE KeyHandle, HANDLE FileHandle, ULONG Format)
{
    return sys_NtSaveKeyEx(KeyHandle, FileHandle, Format);
}

static void LoadNtSaveKeyEx()
{
    *appbox::HookNtSaveKeyEx.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtSaveKeyEx.name);
}

appbox::HookRecord appbox::HookNtSaveKeyEx = {
    "NtSaveKeyEx",
    LoadNtSaveKeyEx,
    (void**)&sys_NtSaveKeyEx,
    Hook_NtSaveKeyEx,
};
