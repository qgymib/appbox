#ifndef APPBOX_SANDBOX_HOOK_RTLQUERYENVIRONMENTVARIABLE_U_HPP
#define APPBOX_SANDBOX_HOOK_RTLQUERYENVIRONMENTVARIABLE_U_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://ntdoc.m417z.com/rtlqueryenvironmentvariable_u
 */
typedef NTSTATUS(NTAPI* T_RtlQueryEnvironmentVariable_U)(PWSTR Environment, PUNICODE_STRING Name, PUNICODE_STRING Value,
                                                         PULONG ReturnLength);

/**
 * @brief RtlQueryEnvironmentVariable_U() direct call.
 */
extern T_RtlQueryEnvironmentVariable_U sys_RtlQueryEnvironmentVariable_U;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook RtlQueryEnvironmentVariable_U().
 */
extern HookRecord HookRtlQueryEnvironmentVariable_U;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_RTLQUERYENVIRONMENTVARIABLE_U_HPP
