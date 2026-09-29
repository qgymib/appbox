#ifndef APPBOX_SANDBOX_HOOK_SETENVIRONMENTVARIABLEA_HPP
#define APPBOX_SANDBOX_HOOK_SETENVIRONMENTVARIABLEA_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-setenvironmentvariablea
 */
typedef BOOL(WINAPI* T_SetEnvironmentVariableA)(LPCSTR lpName, LPCSTR lpValue);

/**
 * @brief SetEnvironmentVariableA() direct call.
 */
extern T_SetEnvironmentVariableA sys_SetEnvironmentVariableA;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook SetEnvironmentVariableA().
 */
extern HookRecord HookSetEnvironmentVariableA;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_SETENVIRONMENTVARIABLEA_HPP
