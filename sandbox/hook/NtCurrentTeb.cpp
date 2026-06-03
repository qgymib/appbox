#include "NtCurrentTeb.hpp"
#include <winnt.h>

T_NtCurrentTeb sys_NtCurrentTeb = nullptr;

static _TEB* LocalNtCurrentTeb()
{
    return NtCurrentTeb();
}

static void LoadNtCurrentTeb()
{
    sys_NtCurrentTeb = LocalNtCurrentTeb;
}

appbox::HookRecord appbox::HookNtCurrentTeb = {
    "NtCurrentTeb",
    LoadNtCurrentTeb,
    (void**)&sys_NtCurrentTeb,
    nullptr,
};
