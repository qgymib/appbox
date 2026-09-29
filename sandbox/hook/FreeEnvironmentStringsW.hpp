#ifndef APPBOX_SANDBOX_HOOK_FREEENVIRONMENTSTRINGSW_HPP
#define APPBOX_SANDBOX_HOOK_FREEENVIRONMENTSTRINGSW_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-freeenvironmentstringsw
 */
typedef BOOL(WINAPI* T_FreeEnvironmentStringsW)(LPWCH lpszEnvironmentBlock);

/**
 * @brief FreeEnvironmentStringsW() direct call.
 */
extern T_FreeEnvironmentStringsW sys_FreeEnvironmentStringsW;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook FreeEnvironmentStringsW().
 */
extern HookRecord HookFreeEnvironmentStringsW;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_FREEENVIRONMENTSTRINGSW_HPP
