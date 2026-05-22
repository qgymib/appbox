#include "RtlInitUnicodeString.hpp"
#include "__init__.hpp"

T_RtlInitUnicodeString sys_RtlInitUnicodeString = nullptr;

void appbox::InjectRtlInitUnicodeString()
{
    sys_RtlInitUnicodeString = (T_RtlInitUnicodeString)GetProcAddress(sys.h_ntdll, "RtlInitUnicodeString");
}
