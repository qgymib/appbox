#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include "network/Completion.hpp"
#include "WSASend.hpp"

T_WSASend sys_WSASend = nullptr;

/**
 * @brief Detour of WSASend().
 *
 * The buffers of a datagram socket which is connected are wrapped by the
 * protocol of the proxy and sent to the relay of the association. An overlapped
 * call is performed by the sandbox and reported the way the stack would have
 * reported it, because the payload of the protocol is not the payload of the
 * application and cannot be handed to the stack in its buffers.
 */
static int WSAAPI Hook_WSASend(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent,
                               DWORD dwFlags, LPWSAOVERLAPPED lpOverlapped,
                               LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    appbox::network::Proxy*           proxy = appbox::network::ProxyOfProcess();
    appbox::network::socks5::Endpoint target;
    if (proxy == nullptr || lpBuffers == nullptr || dwBufferCount == 0 || !proxy->ProxiesDatagram(s) ||
        !proxy->PeerOf(s, target))
    {
        return sys_WSASend(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpOverlapped,
                           lpCompletionRoutine);
    }

    const int sent = proxy->SendDatagram(s, target, lpBuffers, dwBufferCount);
    if (sent == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }

    if (lpOverlapped != nullptr)
    {
        appbox::network::CompleteOverlapped(s, lpOverlapped, static_cast<DWORD>(sent), 0);
        WSASetLastError(WSA_IO_PENDING);
        return SOCKET_ERROR;
    }

    if (lpNumberOfBytesSent != nullptr)
    {
        *lpNumberOfBytesSent = static_cast<DWORD>(sent);
    }
    return 0;
}

static void LoadWSASend()
{
    sys_WSASend = reinterpret_cast<T_WSASend>(GetProcAddress(appbox::sys.h_ws2_32, "WSASend"));
}

appbox::HookRecord appbox::HookWSASend = {
    "WSASend",
    LoadWSASend,
    (void**)&sys_WSASend,
    Hook_WSASend,
};
