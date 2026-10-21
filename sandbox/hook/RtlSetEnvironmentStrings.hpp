#ifndef APPBOX_SANDBOX_HOOK_RTLSETENVIRONMENTSTRINGS_HPP
#define APPBOX_SANDBOX_HOOK_RTLSETENVIRONMENTSTRINGS_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://ntdoc.m417z.com/rtlsetenvironmentstrings
 */
typedef NTSTATUS(NTAPI* T_RtlSetEnvironmentStrings)(PVOID NewEnvironment, SIZE_T Size);

/**
 * @brief RtlSetEnvironmentStrings() direct call.
 */
extern T_RtlSetEnvironmentStrings sys_RtlSetEnvironmentStrings;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook RtlSetEnvironmentStrings().
 */
extern HookRecord HookRtlSetEnvironmentStrings;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_RTLSETENVIRONMENTSTRINGS_HPP
