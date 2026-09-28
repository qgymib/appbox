#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include "WSARecvFrom.hpp"
#include <cstring>

T_WSARecvFrom sys_WSARecvFrom = nullptr;

/**
 * @brief Hand the payload of a datagram over to the caller.
 *
 * The payload is scattered over the buffers of the caller the way the call of
 * the application asks for, and the caller is answered with the address the
 * payload was sent to instead of the address of the relay.
 *
 * @param[in] payload Bytes of the payload, may be null while its size is zero.
 * @param[in] size Number of the bytes of the payload.
 * @param[in] buffers Buffers of the caller.
 * @param[in] count Number of the buffers of the caller.
 * @param[out] from Address of the caller, may be null.
 * @param[in,out] fromlen Size of the address buffer, replaced by the size of
 *                        the address.
 * @param[in] origin Address the payload was sent to.
 * @param[in] family Family of the address the socket works with.
 * @param[out] received Number of the bytes which were handed over.
 * @return 0 on success, `SOCKET_ERROR` when the payload did not fit.
 */
static int Deliver(const std::uint8_t* payload, std::size_t size, const WSABUF* buffers, std::size_t count,
                   sockaddr* from, int* fromlen, const appbox::network::socks5::Endpoint& origin, int family,
                   DWORD& received)
{
    std::size_t copied = 0;
    for (std::size_t index = 0; index < count && copied < size; ++index)
    {
        if (buffers[index].buf == nullptr)
        {
            continue;
        }

        const std::size_t room = buffers[index].len;
        const std::size_t take = (size - copied) < room ? (size - copied) : room;
        if (take != 0 && payload != nullptr)
        {
            std::memcpy(buffers[index].buf, payload + copied, take);
        }
        copied += take;
    }

    if (from != nullptr && fromlen != nullptr)
    {
        if (!appbox::network::WriteAddress(origin, family, from, fromlen))
        {
            LOG_W("the address of a datagram cannot be reported to the application");
        }
    }

    received = static_cast<DWORD>(copied);
    if (size > copied)
    {
        /* The datagram is longer than the buffers of the caller. */
        WSASetLastError(WSAEMSGSIZE);
        return SOCKET_ERROR;
    }
    return 0;
}

/**
 * @brief Detour of WSARecvFrom().
 *
 * A datagram which came from the relay of the association of the socket is
 * unwrapped, and the caller is answered with the address the payload was sent
 * to. A datagram which the caller asks for as an overlapped operation, and
 * every socket of a process which has no association, keeps the path of the
 * host.
 */
static int WSAAPI Hook_WSARecvFrom(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd,
                                   LPDWORD lpFlags, sockaddr* lpFrom, LPINT lpFromlen, LPWSAOVERLAPPED lpOverlapped,
                                   LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    appbox::network::Proxy* proxy = appbox::network::ProxyOfProcess();
    if (proxy == nullptr || lpBuffers == nullptr || dwBufferCount == 0 || !proxy->HasAssociation(s))
    {
        return sys_WSARecvFrom(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpFrom, lpFromlen,
                               lpOverlapped, lpCompletionRoutine);
    }

    if (lpOverlapped != nullptr)
    {
        LOG_W("an overlapped datagram keeps the direct path");
        return sys_WSARecvFrom(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpFrom, lpFromlen,
                               lpOverlapped, lpCompletionRoutine);
    }

    std::size_t room = 0;
    for (DWORD index = 0; index < dwBufferCount; ++index)
    {
        room += lpBuffers[index].len;
    }

    /* The datagram is read with room for the header of the protocol. */
    std::vector<std::uint8_t>& scratch = appbox::network::IncomingBuffer();
    const std::size_t          capacity = room + appbox::network::socks5::kMaxUdpHeaderSize;
    scratch.resize(capacity);

    sockaddr_storage source;
    int              source_length = sizeof(source);
    ZeroMemory(&source, sizeof(source));

    WSABUF buffer;
    buffer.len = static_cast<ULONG>(capacity);
    buffer.buf = reinterpret_cast<char*>(scratch.data());

    DWORD     read = 0;
    DWORD     flags = lpFlags != nullptr ? *lpFlags : 0;
    const int result = sys_WSARecvFrom(s, &buffer, 1, &read, &flags, reinterpret_cast<sockaddr*>(&source),
                                       &source_length, nullptr, nullptr);
    if (result == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    if (lpFlags != nullptr)
    {
        *lpFlags = flags;
    }

    const int family = reinterpret_cast<const sockaddr*>(&source)->sa_family;

    appbox::network::socks5::Endpoint sender;
    if (!appbox::network::EndpointFromSockaddr(reinterpret_cast<const sockaddr*>(&source), source_length, sender) ||
        !proxy->IsRelayOf(s, sender))
    {
        DWORD     received = 0;
        const int delivered =
            Deliver(scratch.data(), read, lpBuffers, dwBufferCount, lpFrom, lpFromlen, sender, family, received);
        if (lpNumberOfBytesRecvd != nullptr)
        {
            *lpNumberOfBytesRecvd = received;
        }
        return delivered;
    }

    appbox::network::socks5::Endpoint origin;
    const std::uint8_t*               payload = nullptr;
    std::size_t                       payload_size = 0;
    if (!appbox::network::socks5::ParseUdpDatagram(scratch.data(), read, origin, payload, payload_size))
    {
        LOG_W("a datagram of the relay of the proxy is not a datagram of the protocol");
        DWORD     received = 0;
        const int delivered =
            Deliver(nullptr, 0, lpBuffers, dwBufferCount, lpFrom, lpFromlen, sender, family, received);
        if (lpNumberOfBytesRecvd != nullptr)
        {
            *lpNumberOfBytesRecvd = received;
        }
        return delivered;
    }

    DWORD     received = 0;
    const int delivered =
        Deliver(payload, payload_size, lpBuffers, dwBufferCount, lpFrom, lpFromlen, origin, family, received);
    if (lpNumberOfBytesRecvd != nullptr)
    {
        *lpNumberOfBytesRecvd = received;
    }
    return delivered;
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
