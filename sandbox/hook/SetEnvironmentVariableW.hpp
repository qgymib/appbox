#ifndef APPBOX_SANDBOX_HOOK_SETENVIRONMENTVARIABLEW_HPP
#define APPBOX_SANDBOX_HOOK_SETENVIRONMENTVARIABLEW_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-setenvironmentvariablew
 */
typedef BOOL(WINAPI* T_SetEnvironmentVariableW)(LPCWSTR lpName, LPCWSTR lpValue);

/**
 * @brief SetEnvironmentVariableW() direct call.
 */
extern T_SetEnvironmentVariableW sys_SetEnvironmentVariableW;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook SetEnvironmentVariableW().
 */
extern HookRecord HookSetEnvironmentVariableW;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_SETENVIRONMENTVARIABLEW_HPP
