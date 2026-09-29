#ifndef APPBOX_SANDBOX_HOOK_GETENVIRONMENTVARIABLEA_HPP
#define APPBOX_SANDBOX_HOOK_GETENVIRONMENTVARIABLEA_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentvariablea
 */
typedef DWORD(WINAPI* T_GetEnvironmentVariableA)(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize);

/**
 * @brief GetEnvironmentVariableA() direct call.
 */
extern T_GetEnvironmentVariableA sys_GetEnvironmentVariableA;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook GetEnvironmentVariableA().
 */
extern HookRecord HookGetEnvironmentVariableA;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_GETENVIRONMENTVARIABLEA_HPP
