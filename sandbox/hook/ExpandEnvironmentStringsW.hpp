#ifndef APPBOX_SANDBOX_HOOK_EXPANDENVIRONMENTSTRINGSW_HPP
#define APPBOX_SANDBOX_HOOK_EXPANDENVIRONMENTSTRINGSW_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-expandenvironmentstringsw
 */
typedef DWORD(WINAPI* T_ExpandEnvironmentStringsW)(LPCWSTR lpSrc, LPWSTR lpDst, DWORD nSize);

/**
 * @brief ExpandEnvironmentStringsW() direct call.
 */
extern T_ExpandEnvironmentStringsW sys_ExpandEnvironmentStringsW;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook ExpandEnvironmentStringsW().
 */
extern HookRecord HookExpandEnvironmentStringsW;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_EXPANDENVIRONMENTSTRINGSW_HPP
