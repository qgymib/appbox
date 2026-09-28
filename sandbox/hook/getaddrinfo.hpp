#ifndef APPBOX_SANDBOX_HOOK_GETADDRINFO_HPP
#define APPBOX_SANDBOX_HOOK_GETADDRINFO_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfo
 */
typedef INT(WSAAPI* T_getaddrinfo)(PCSTR NodeName, PCSTR ServiceName, const ADDRINFOA* Hints, PADDRINFOA* Result);

/**
 * @brief getaddrinfo() direct call.
 */
extern T_getaddrinfo sys_getaddrinfo;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook getaddrinfo().
 */
extern HookRecord HookGetAddrInfo;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_GETADDRINFO_HPP
