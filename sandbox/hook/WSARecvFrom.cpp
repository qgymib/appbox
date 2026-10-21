#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include "network/Completion.hpp"
#include "WSARecvFrom.hpp"

T_WSARecvFrom sys_WSARecvFrom = nullptr;

/**
 * @brief Detour of WSARecvFrom().
 *
 * A datagram of the relay of the association is unwrapped and handed to the
 * caller together with the address the payload was sent to, so the relay is
 * invisible to the application. An overlapped call is handed to the worker of
 * the sandbox, which waits for the datagram and fills the buffers of the caller
 * when it arrives. A socket without an association keeps the path of the host.
 */
static int WSAAPI Hook_WSARecvFrom(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd,
                                   LPDWORD lpFlags, sockaddr* lpFrom, LPINT lpFromlen, LPWSAOVERLAPPED lpOverlapped,
                                   LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    appbox::network::Proxy* proxy = appbox::network::ProxyOfProcess();
    if (proxy == nullptr || lpBuffers == nullptr || dwBufferCount == 0 || !proxy->ProxiesDatagram(s))
    {
        return sys_WSARecvFrom(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpFrom, lpFromlen,
                               lpOverlapped, lpCompletionRoutine);
    }

    if (lpOverlapped != nullptr)
    {
        if (!appbox::network::QueueDatagramReceive(s, lpBuffers, dwBufferCount, lpFrom != nullptr, lpFrom, lpFromlen,
                                                   lpOverlapped, AF_UNSPEC))
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

    if (lpFrom != nullptr && lpFromlen != nullptr &&
        !appbox::network::WriteAddress(source, AF_UNSPEC, lpFrom, lpFromlen))
    {
        LOG_W("the address of a datagram cannot be reported to the application");
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

static void LoadWSARecvFrom()
{
    sys_WSARecvFrom = reinterpret_cast<T_WSARecvFrom>(GetProcAddress(appbox::sys.h_ws2_32, "WSARecvFrom"));
}

appbox::HookRecord appbox::HookWSARecvFrom = {
    "WSARecvFrom",
    LoadWSARecvFrom,
    (void**)&sys_WSARecvFrom,
    Hook_WSARecvFrom,
};
