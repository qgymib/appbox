#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include "WSASendTo.hpp"

T_WSASendTo sys_WSASendTo = nullptr;

/**
 * @brief Detour of WSASendTo().
 *
 * The buffers of the caller are wrapped by the protocol of the proxy and sent
 * to the relay of the association of the socket, and the call reports the
 * number of the payload bytes which were handed over.
 *
 * A datagram which the caller sends as an overlapped operation keeps the path
 * of the host: the message of the protocol is built in the buffer of the
 * sandbox, which the stack reads after the call returns, so an overlapped send
 * cannot be carried without a completion of its own.
 */
static int WSAAPI Hook_WSASendTo(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent,
                                 DWORD dwFlags, const sockaddr* lpTo, int iTolen, LPWSAOVERLAPPED lpOverlapped,
                                 LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    appbox::network::Proxy* proxy = appbox::network::ProxyOfProcess();
    if (proxy == nullptr || lpBuffers == nullptr || lpTo == nullptr || dwBufferCount == 0 || !proxy->ProxiesDatagram(s))
    {
        return sys_WSASendTo(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpTo, iTolen, lpOverlapped,
                             lpCompletionRoutine);
    }

    if (lpOverlapped != nullptr)
    {
        LOG_W("an overlapped datagram keeps the direct path");
        return sys_WSASendTo(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpTo, iTolen, lpOverlapped,
                             lpCompletionRoutine);
    }

    appbox::network::socks5::Endpoint target;
    if (!appbox::network::EndpointFromSockaddr(lpTo, iTolen, target))
    {
        LOG_W("the address of a datagram cannot be carried by the proxy");
        return sys_WSASendTo(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpTo, iTolen, lpOverlapped,
                             lpCompletionRoutine);
    }

    const int sent = proxy->SendDatagram(s, target, lpBuffers, dwBufferCount);
    if (sent == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }

    if (lpNumberOfBytesSent != nullptr)
    {
        *lpNumberOfBytesSent = static_cast<DWORD>(sent);
    }
    return 0;
}

static void LoadWSASendTo()
{
    sys_WSASendTo = reinterpret_cast<T_WSASendTo>(GetProcAddress(appbox::sys.h_ws2_32, "WSASendTo"));
}

appbox::HookRecord appbox::HookWSASendTo = {
    "WSASendTo",
    LoadWSASendTo,
    (void**)&sys_WSASendTo,
    Hook_WSASendTo,
};
