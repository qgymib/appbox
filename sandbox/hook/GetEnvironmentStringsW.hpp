#ifndef APPBOX_SANDBOX_HOOK_GETENVIRONMENTSTRINGSW_HPP
#define APPBOX_SANDBOX_HOOK_GETENVIRONMENTSTRINGSW_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentstringsw
 */
typedef LPWCH(WINAPI* T_GetEnvironmentStringsW)();

/**
 * @brief GetEnvironmentStringsW() direct call.
 */
extern T_GetEnvironmentStringsW sys_GetEnvironmentStringsW;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook GetEnvironmentStringsW().
 */
extern HookRecord HookGetEnvironmentStringsW;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_GETENVIRONMENTSTRINGSW_HPP
