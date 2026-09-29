#ifndef APPBOX_SANDBOX_HOOK_RTLQUERYENVIRONMENTVARIABLE_HPP
#define APPBOX_SANDBOX_HOOK_RTLQUERYENVIRONMENTVARIABLE_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://ntdoc.m417z.com/rtlqueryenvironmentvariable
 */
typedef NTSTATUS(NTAPI* T_RtlQueryEnvironmentVariable)(PWSTR Environment, PWSTR Name, SIZE_T NameLength, PWSTR Value,
                                                       SIZE_T ValueLength, PSIZE_T ReturnLength);

/**
 * @brief RtlQueryEnvironmentVariable() direct call.
 */
extern T_RtlQueryEnvironmentVariable sys_RtlQueryEnvironmentVariable;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook RtlQueryEnvironmentVariable().
 */
extern HookRecord HookRtlQueryEnvironmentVariable;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_RTLQUERYENVIRONMENTVARIABLE_HPP
