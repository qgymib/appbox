#ifndef APPBOX_SANDBOX_HOOK_WSAIOCTL_HPP
#define APPBOX_SANDBOX_HOOK_WSAIOCTL_HPP

#include "utils/Winsock.hpp"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaioctl
 */
typedef int(WSAAPI* T_WSAIoctl)(SOCKET s, DWORD dwIoControlCode, LPVOID lpvInBuffer, DWORD cbInBuffer,
                                LPVOID lpvOutBuffer, DWORD cbOutBuffer, LPDWORD lpcbBytesReturned,
                                LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);

/**
 * @brief WSAIoctl() direct call.
 */
extern T_WSAIoctl sys_WSAIoctl;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook WSAIoctl().
 */
extern HookRecord HookWSAIoctl;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_WSAIOCTL_HPP
