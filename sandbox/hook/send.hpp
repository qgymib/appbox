#ifndef APPBOX_SANDBOX_HOOK_SEND_HPP
#define APPBOX_SANDBOX_HOOK_SEND_HPP

#include "utils/Winsock.hpp"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-send
 */
typedef int(WSAAPI* T_send)(SOCKET s, const char* buf, int len, int flags);

/**
 * @brief send() direct call.
 */
extern T_send sys_send;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook send().
 */
extern HookRecord HookSend;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_SEND_HPP
