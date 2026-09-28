#ifndef APPBOX_SANDBOX_HOOK_RECVFROM_HPP
#define APPBOX_SANDBOX_HOOK_RECVFROM_HPP

#include "utils/Winsock.hpp"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-recvfrom
 */
typedef int(WSAAPI* T_recvfrom)(SOCKET s, char* buf, int len, int flags, sockaddr* from, int* fromlen);

/**
 * @brief recvfrom() direct call.
 */
extern T_recvfrom sys_recvfrom;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook recvfrom().
 */
extern HookRecord HookRecvFrom;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_RECVFROM_HPP
