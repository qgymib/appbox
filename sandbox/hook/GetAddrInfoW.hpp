#ifndef APPBOX_SANDBOX_HOOK_GETADDRINFOW_HPP
#define APPBOX_SANDBOX_HOOK_GETADDRINFOW_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfow
 */
typedef INT(WSAAPI* T_GetAddrInfoW)(PCWSTR NodeName, PCWSTR ServiceName, const ADDRINFOW* Hints, PADDRINFOW* Result);

/**
 * @brief GetAddrInfoW() direct call.
 */
extern T_GetAddrInfoW sys_GetAddrInfoW;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook GetAddrInfoW().
 */
extern HookRecord HookGetAddrInfoW;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_GETADDRINFOW_HPP
