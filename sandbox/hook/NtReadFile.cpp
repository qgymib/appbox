#include "NtReadFile.hpp"

T_NtReadFile sys_NtReadFile = nullptr;

static void LoadNtReadFile()
{
    *appbox::HookNtReadFile.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtReadFile.name);
}

appbox::HookRecord appbox::HookNtReadFile = {
    "NtReadFile",
    LoadNtReadFile,
    (void**)&sys_NtReadFile,
    nullptr,
};
