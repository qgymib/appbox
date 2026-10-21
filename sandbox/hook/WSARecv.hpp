#ifndef APPBOX_SANDBOX_HOOK_WSARECV_HPP
#define APPBOX_SANDBOX_HOOK_WSARECV_HPP

#include "utils/Winsock.hpp"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsarecv
 */
typedef int(WSAAPI* T_WSARecv)(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd,
                               LPDWORD lpFlags, LPWSAOVERLAPPED lpOverlapped,
                               LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);

/**
 * @brief WSARecv() direct call.
 */
extern T_WSARecv sys_WSARecv;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook WSARecv().
 */
extern HookRecord HookWSARecv;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_WSARECV_HPP
