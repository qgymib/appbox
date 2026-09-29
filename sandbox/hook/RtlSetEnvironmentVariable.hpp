#ifndef APPBOX_SANDBOX_HOOK_RTLSETENVIRONMENTVARIABLE_HPP
#define APPBOX_SANDBOX_HOOK_RTLSETENVIRONMENTVARIABLE_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://ntdoc.m417z.com/rtlsetenvironmentvariable
 */
typedef NTSTATUS(NTAPI* T_RtlSetEnvironmentVariable)(PWSTR* Environment, PUNICODE_STRING Name, PUNICODE_STRING Value);

/**
 * @brief RtlSetEnvironmentVariable() direct call.
 */
extern T_RtlSetEnvironmentVariable sys_RtlSetEnvironmentVariable;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook RtlSetEnvironmentVariable().
 */
extern HookRecord HookRtlSetEnvironmentVariable;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_RTLSETENVIRONMENTVARIABLE_HPP
