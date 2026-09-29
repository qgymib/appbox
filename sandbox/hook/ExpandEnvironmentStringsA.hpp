#ifndef APPBOX_SANDBOX_HOOK_EXPANDENVIRONMENTSTRINGSA_HPP
#define APPBOX_SANDBOX_HOOK_EXPANDENVIRONMENTSTRINGSA_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-expandenvironmentstringsa
 */
typedef DWORD(WINAPI* T_ExpandEnvironmentStringsA)(LPCSTR lpSrc, LPSTR lpDst, DWORD nSize);

/**
 * @brief ExpandEnvironmentStringsA() direct call.
 */
extern T_ExpandEnvironmentStringsA sys_ExpandEnvironmentStringsA;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook ExpandEnvironmentStringsA().
 */
extern HookRecord HookExpandEnvironmentStringsA;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_EXPANDENVIRONMENTSTRINGSA_HPP
