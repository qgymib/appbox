#include "RtlInitUnicodeString.hpp"
#include "__init__.hpp"

T_RtlInitUnicodeString sys_RtlInitUnicodeString = nullptr;

void appbox::AttachRtlInitUnicodeString()
{
    sys_RtlInitUnicodeString = (T_RtlInitUnicodeString)GetProcAddress(sys.h_ntdll, "RtlInitUnicodeString");
}

void appbox::DetachRtlInitUnicodeString()
{
}
