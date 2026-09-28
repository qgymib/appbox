#ifndef APPBOX_SANDBOX_HOOK_WSACONNECT_HPP
#define APPBOX_SANDBOX_HOOK_WSACONNECT_HPP

#include "utils/Winsock.hpp"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaconnect
 */
typedef int(WSAAPI* T_WSAConnect)(SOCKET s, const sockaddr* name, int namelen, LPWSABUF lpCallerData,
                                  LPWSABUF lpCalleeData, LPQOS lpSQOS, LPQOS lpGQOS);

/**
 * @brief WSAConnect() direct call.
 */
extern T_WSAConnect sys_WSAConnect;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook WSAConnect().
 */
extern HookRecord HookWSAConnect;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_WSACONNECT_HPP
