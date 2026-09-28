#ifndef APPBOX_SANDBOX_HOOK_SENDTO_HPP
#define APPBOX_SANDBOX_HOOK_SENDTO_HPP

#include "utils/Winsock.hpp"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-sendto
 */
typedef int(WSAAPI* T_sendto)(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int tolen);

/**
 * @brief sendto() direct call.
 */
extern T_sendto sys_sendto;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook sendto().
 */
extern HookRecord HookSendTo;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_SENDTO_HPP
