#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include "network/Completion.hpp"
#include "WSARecv.hpp"

T_WSARecv sys_WSARecv = nullptr;

/**
 * @brief Detour of WSARecv().
 *
 * A datagram socket which is connected receives the payload of a datagram of
 * the relay. An overlapped call is handed to the worker of the sandbox, which
 * waits for the datagram and fills the buffers of the application when it
 * arrives, because the message of the protocol cannot be read into the buffers
 * of the application directly.
 */
static int WSAAPI Hook_WSARecv(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd,
                               LPDWORD lpFlags, LPWSAOVERLAPPED lpOverlapped,
                               LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    appbox::network::Proxy* proxy = appbox::network::ProxyOfProcess();
    if (proxy == nullptr || lpBuffers == nullptr || dwBufferCount == 0 || !proxy->ProxiesDatagram(s))
    {
        return sys_WSARecv(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpOverlapped,
                           lpCompletionRoutine);
    }

    if (lpOverlapped != nullptr)
    {
        if (!appbox::network::QueueDatagramReceive(s, lpBuffers, dwBufferCount, false, nullptr, nullptr, lpOverlapped,
                                                   AF_UNSPEC))
        {
            WSASetLastError(WSAENOBUFS);
            return SOCKET_ERROR;
        }
        WSASetLastError(WSA_IO_PENDING);
        return SOCKET_ERROR;
    }

    appbox::network::socks5::Endpoint source;
    const DWORD                       deadline = GetTickCount() + 30000;
    const int                         received = proxy->ReceiveDatagram(s, lpBuffers, dwBufferCount, source, deadline);
    if (received == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }

    if (lpNumberOfBytesRecvd != nullptr)
    {
        *lpNumberOfBytesRecvd = static_cast<DWORD>(received);
    }
    if (lpFlags != nullptr)
    {
        *lpFlags = 0;
    }
    return 0;
}

static void LoadWSARecv()
{
    sys_WSARecv = reinterpret_cast<T_WSARecv>(GetProcAddress(appbox::sys.h_ws2_32, "WSARecv"));
}

appbox::HookRecord appbox::HookWSARecv = {
    "WSARecv",
    LoadWSARecv,
    (void**)&sys_WSARecv,
    Hook_WSARecv,
};
