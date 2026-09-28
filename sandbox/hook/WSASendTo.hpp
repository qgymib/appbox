#ifndef APPBOX_SANDBOX_HOOK_WSASENDTO_HPP
#define APPBOX_SANDBOX_HOOK_WSASENDTO_HPP

#include "utils/Winsock.hpp"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsasendto
 */
typedef int(WSAAPI* T_WSASendTo)(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent,
                                 DWORD dwFlags, const sockaddr* lpTo, int iTolen, LPWSAOVERLAPPED lpOverlapped,
                                 LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);

/**
 * @brief WSASendTo() direct call.
 */
extern T_WSASendTo sys_WSASendTo;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook WSASendTo().
 */
extern HookRecord HookWSASendTo;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_WSASENDTO_HPP
