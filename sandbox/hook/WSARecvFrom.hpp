#ifndef APPBOX_SANDBOX_HOOK_WSARECVFROM_HPP
#define APPBOX_SANDBOX_HOOK_WSARECVFROM_HPP

#include "utils/Winsock.hpp"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsarecvfrom
 */
typedef int(WSAAPI* T_WSARecvFrom)(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd,
                                   LPDWORD lpFlags, sockaddr* lpFrom, LPINT lpFromlen, LPWSAOVERLAPPED lpOverlapped,
                                   LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);

/**
 * @brief WSARecvFrom() direct call.
 */
extern T_WSARecvFrom sys_WSARecvFrom;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook WSARecvFrom().
 */
extern HookRecord HookWSARecvFrom;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_WSARECVFROM_HPP
