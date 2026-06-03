#include "NtReadFile.hpp"

T_NtReadFile sys_NtReadFile = nullptr;

static void LoadNtReadFile()
{
    sys_NtReadFile = (T_NtReadFile)GetProcAddress(appbox::sys.h_ntdll, "NtReadFile");
}

appbox::HookRecord appbox::HookNtReadFile = {
    "NtReadFile",
    LoadNtReadFile,
    (void**)&sys_NtReadFile,
    nullptr,
};
