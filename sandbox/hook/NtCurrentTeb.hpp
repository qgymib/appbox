#ifndef APPBOX_SANDBOX_HOOK_NTCURRENTTEB_HPP
#define APPBOX_SANDBOX_HOOK_NTCURRENTTEB_HPP

#include "WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-ntcurrentteb
 *
 * `NtCurrentTeb` is not an export of ntdll: it is a function of this module
 * which reads the TEB of the current thread, so the pointer carries no calling
 * convention of its own. Every other typedef of the hooks names an export and
 * therefore has to spell `NTAPI`/`WINAPI`, which is what makes the call work in
 * a 32 bit process.
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
