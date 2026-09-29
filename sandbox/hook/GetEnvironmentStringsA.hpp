#ifndef APPBOX_SANDBOX_HOOK_GETENVIRONMENTSTRINGSA_HPP
#define APPBOX_SANDBOX_HOOK_GETENVIRONMENTSTRINGSA_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentstringsa
 */
typedef LPCH(WINAPI* T_GetEnvironmentStringsA)();

/**
 * @brief GetEnvironmentStringsA() direct call.
 */
extern T_GetEnvironmentStringsA sys_GetEnvironmentStringsA;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook GetEnvironmentStringsA().
 */
extern HookRecord HookGetEnvironmentStringsA;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_GETENVIRONMENTSTRINGSA_HPP
