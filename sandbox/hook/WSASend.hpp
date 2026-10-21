#ifndef APPBOX_SANDBOX_HOOK_WSASEND_HPP
#define APPBOX_SANDBOX_HOOK_WSASEND_HPP

#include "utils/Winsock.hpp"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsasend
 */
typedef int(WSAAPI* T_WSASend)(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent,
                               DWORD dwFlags, LPWSAOVERLAPPED lpOverlapped,
                               LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);

/**
 * @brief WSASend() direct call.
 */
extern T_WSASend sys_WSASend;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook WSASend().
 */
extern HookRecord HookWSASend;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_WSASEND_HPP
