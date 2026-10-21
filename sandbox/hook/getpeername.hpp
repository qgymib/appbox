#ifndef APPBOX_SANDBOX_HOOK_GETPEERNAME_HPP
#define APPBOX_SANDBOX_HOOK_GETPEERNAME_HPP

#include "utils/Winsock.hpp"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-getpeername
 */
typedef int(WSAAPI* T_getpeername)(SOCKET s, sockaddr* name, int* namelen);

/**
 * @brief getpeername() direct call.
 */
extern T_getpeername sys_getpeername;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook getpeername().
 */
extern HookRecord HookGetPeerName;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_GETPEERNAME_HPP
