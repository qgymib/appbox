#ifndef APPBOX_SANDBOX_HOOK_NTCURRENTTEB_HPP
#define APPBOX_SANDBOX_HOOK_NTCURRENTTEB_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-ntcurrentteb
 */
typedef _TEB* (*T_NtCurrentTeb)();

/**
 * @brief NtCurrentTeb() direct call.
 */
extern T_NtCurrentTeb sys_NtCurrentTeb;
}

namespace appbox
{

/**
 * @brief Hook NtCurrentTeb().
 */
extern HookRecord HookNtCurrentTeb;

} // namespace appbox

#endif
