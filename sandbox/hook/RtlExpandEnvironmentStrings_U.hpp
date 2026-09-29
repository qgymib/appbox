#ifndef APPBOX_SANDBOX_HOOK_RTLEXPANDENVIRONMENTSTRINGS_U_HPP
#define APPBOX_SANDBOX_HOOK_RTLEXPANDENVIRONMENTSTRINGS_U_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://ntdoc.m417z.com/rtlexpandenvironmentstrings_u
 */
typedef NTSTATUS(NTAPI* T_RtlExpandEnvironmentStrings_U)(PWSTR Environment, PUNICODE_STRING Source,
                                                         PUNICODE_STRING Destination, PULONG ReturnLength);

/**
 * @brief RtlExpandEnvironmentStrings_U() direct call.
 */
extern T_RtlExpandEnvironmentStrings_U sys_RtlExpandEnvironmentStrings_U;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook RtlExpandEnvironmentStrings_U().
 */
extern HookRecord HookRtlExpandEnvironmentStrings_U;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_RTLEXPANDENVIRONMENTSTRINGS_U_HPP
