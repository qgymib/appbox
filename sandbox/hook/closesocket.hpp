#ifndef APPBOX_SANDBOX_HOOK_CLOSESOCKET_HPP
#define APPBOX_SANDBOX_HOOK_CLOSESOCKET_HPP

#include "utils/Winsock.hpp"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-closesocket
 */
typedef int(WSAAPI* T_closesocket)(SOCKET s);

/**
 * @brief closesocket() direct call.
 */
extern T_closesocket sys_closesocket;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook closesocket().
 */
extern HookRecord HookCloseSocket;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_CLOSESOCKET_HPP
