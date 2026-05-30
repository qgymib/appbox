#include "NtReadFile.hpp"
#include "__init__.hpp"

T_NtReadFile sys_NtReadFile = nullptr;

void appbox::AttachNtReadFile()
{
    sys_NtReadFile = (T_NtReadFile)GetProcAddress(sys.h_ntdll, "NtReadFile");
}

void appbox::DetachNtReadFile()
{
}
