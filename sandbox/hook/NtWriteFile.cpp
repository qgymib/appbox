#include "NtWriteFile.hpp"
#include "__init__.hpp"

T_NtWriteFile sys_NtWriteFile = nullptr;

void appbox::AttachNtWriteFile()
{
    sys_NtWriteFile = (T_NtWriteFile)GetProcAddress(sys.h_ntdll, "NtWriteFile");
}

void appbox::DetachNtWriteFile()
{
}
