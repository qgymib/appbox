#include "RtlInitUnicodeString.hpp"

T_RtlInitUnicodeString sys_RtlInitUnicodeString = nullptr;

static void LoadRtlInitUnicodeString()
{
    sys_RtlInitUnicodeString = (T_RtlInitUnicodeString)GetProcAddress(appbox::sys.h_ntdll, "RtlInitUnicodeString");
}

appbox::HookRecord appbox::HookRtlInitUnicodeString = {
    "RtlInitUnicodeString",
    LoadRtlInitUnicodeString,
    (void**)&sys_RtlInitUnicodeString,
    nullptr,
};
