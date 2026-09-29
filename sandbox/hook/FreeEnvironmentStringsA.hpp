#ifndef APPBOX_SANDBOX_HOOK_FREEENVIRONMENTSTRINGSA_HPP
#define APPBOX_SANDBOX_HOOK_FREEENVIRONMENTSTRINGSA_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-freeenvironmentstringsa
 */
typedef BOOL(WINAPI* T_FreeEnvironmentStringsA)(LPCH lpszEnvironmentBlock);

/**
 * @brief FreeEnvironmentStringsA() direct call.
 */
extern T_FreeEnvironmentStringsA sys_FreeEnvironmentStringsA;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook FreeEnvironmentStringsA().
 */
extern HookRecord HookFreeEnvironmentStringsA;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_FREEENVIRONMENTSTRINGSA_HPP
