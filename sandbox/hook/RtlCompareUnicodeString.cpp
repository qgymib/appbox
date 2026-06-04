#include "RtlCompareUnicodeString.hpp"

T_RtlCompareUnicodeString sys_RtlCompareUnicodeString = nullptr;

static void LoadRtlCompareUnicodeString()
{
    sys_RtlCompareUnicodeString =
        (T_RtlCompareUnicodeString)GetProcAddress(appbox::sys.h_ntdll, "RtlCompareUnicodeString");
}

appbox::HookRecord appbox::HookRtlCompareUnicodeString = {
    "RtlCompareUnicodeString",
    LoadRtlCompareUnicodeString,
    (void**)&sys_RtlCompareUnicodeString,
    nullptr,
};
