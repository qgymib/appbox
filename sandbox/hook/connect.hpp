#ifndef APPBOX_SANDBOX_HOOK_CONNECT_HPP
#define APPBOX_SANDBOX_HOOK_CONNECT_HPP

#include "utils/Winsock.hpp"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-connect
 */
typedef int(WSAAPI* T_connect)(SOCKET s, const sockaddr* name, int namelen);

/**
 * @brief connect() direct call.
 */
extern T_connect sys_connect;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook connect().
 */
extern HookRecord HookConnect;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_CONNECT_HPP
