#ifndef APPBOX_SANDBOX_HOOK_RTLSETCURRENTENVIRONMENT_HPP
#define APPBOX_SANDBOX_HOOK_RTLSETCURRENTENVIRONMENT_HPP

#include "WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://ntdoc.m417z.com/rtlsetcurrentenvironment
 */
typedef NTSTATUS(NTAPI* T_RtlSetCurrentEnvironment)(PWSTR Environment, PWSTR* PreviousEnvironment);

/**
 * @brief RtlSetCurrentEnvironment() direct call.
 */
extern T_RtlSetCurrentEnvironment sys_RtlSetCurrentEnvironment;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook RtlSetCurrentEnvironment().
 */
extern HookRecord HookRtlSetCurrentEnvironment;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_RTLSETCURRENTENVIRONMENT_HPP
