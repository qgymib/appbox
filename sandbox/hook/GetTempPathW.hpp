#ifndef APPBOX_SANDBOX_HOOK_GETTEMPPATHW_HPP
#define APPBOX_SANDBOX_HOOK_GETTEMPPATHW_HPP

#include "utils/WinAPI.h"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-gettemppathw
 */
typedef DWORD(WINAPI* T_GetTempPathW)(DWORD nBufferLength, LPWSTR lpBuffer);

/**
 * @brief GetTempPathW() direct call.
 */
extern T_GetTempPathW sys_GetTempPathW;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook GetTempPathW().
 */
extern HookRecord HookGetTempPathW;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_GETTEMPPATHW_HPP
