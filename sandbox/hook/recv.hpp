#ifndef APPBOX_SANDBOX_HOOK_RECV_HPP
#define APPBOX_SANDBOX_HOOK_RECV_HPP

#include "utils/Winsock.hpp"
#include "hook/__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-recv
 */
typedef int(WSAAPI* T_recv)(SOCKET s, char* buf, int len, int flags);

/**
 * @brief recv() direct call.
 */
extern T_recv sys_recv;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook recv().
 */
extern HookRecord HookRecv;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_RECV_HPP
