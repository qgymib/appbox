#include "NtWriteFile.hpp"

T_NtWriteFile sys_NtWriteFile = nullptr;

static void LoadNtWriteFile()
{
    sys_NtWriteFile = (T_NtWriteFile)GetProcAddress(appbox::sys.h_ntdll, "NtWriteFile");
}

appbox::HookRecord appbox::HookNtWriteFile = {
    "NtWriteFile",
    LoadNtWriteFile,
    (void**)&sys_NtWriteFile,
    nullptr,
};
