#ifndef APPBOX_SANDBOX_HOOK_RTLCREATEENVIRONMENT_HPP
#define APPBOX_SANDBOX_HOOK_RTLCREATEENVIRONMENT_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://ntdoc.m417z.com/rtlcreateenvironment
 */
typedef NTSTATUS(NTAPI* T_RtlCreateEnvironment)(BOOLEAN Inherit, PWSTR* Environment);

/**
 * @brief RtlCreateEnvironment() direct call.
 */
extern T_RtlCreateEnvironment sys_RtlCreateEnvironment;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook RtlCreateEnvironment().
 */
extern HookRecord HookRtlCreateEnvironment;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_RTLCREATEENVIRONMENT_HPP
