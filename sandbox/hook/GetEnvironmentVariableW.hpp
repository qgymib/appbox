#ifndef APPBOX_SANDBOX_HOOK_GETENVIRONMENTVARIABLEW_HPP
#define APPBOX_SANDBOX_HOOK_GETENVIRONMENTVARIABLEW_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentvariablew
 */
typedef DWORD(WINAPI* T_GetEnvironmentVariableW)(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize);

/**
 * @brief GetEnvironmentVariableW() direct call.
 */
extern T_GetEnvironmentVariableW sys_GetEnvironmentVariableW;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook GetEnvironmentVariableW().
 */
extern HookRecord HookGetEnvironmentVariableW;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_GETENVIRONMENTVARIABLEW_HPP
