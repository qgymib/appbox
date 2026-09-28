#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include "recvfrom.hpp"
#include <cstring>

T_recvfrom sys_recvfrom = nullptr;

/**
 * @brief Hand the payload of a datagram over to the caller.
 *
 * The caller is answered with the address the payload was sent to instead of
 * the address of the relay which carried it, which is what an application
 * expects: the relay is an implementation detail of the proxy.
 *
 * @param[in] payload Bytes of the payload, may be null while its size is zero.
 * @param[in] size Number of the bytes of the payload.
 * @param[out] buf Buffer of the caller.
 * @param[in] len Number of the bytes of the buffer.
 * @param[out] from Address of the caller, may be null.
 * @param[in,out] fromlen Size of the address buffer, replaced by the size of
 *                        the address.
 * @param[in] origin Address the payload was sent to.
 * @param[in] family Family of the address the socket works with.
 * @return The number of the bytes which were handed over, or `SOCKET_ERROR`
 *         when the payload did not fit the buffer.
 */
static int Deliver(const std::uint8_t* payload, std::size_t size, char* buf, int len, sockaddr* from, int* fromlen,
                   const appbox::network::socks5::Endpoint& origin, int family)
{
    const std::size_t copied = size < static_cast<std::size_t>(len) ? size : static_cast<std::size_t>(len);
    if (copied != 0 && payload != nullptr)
    {
        std::memcpy(buf, payload, copied);
    }

    if (from != nullptr && fromlen != nullptr)
    {
        if (!appbox::network::WriteAddress(origin, family, from, fromlen))
        {
            LOG_W("the address of a datagram cannot be reported to the application");
        }
    }

    if (size > copied)
    {
        /* The datagram is longer than the buffer of the caller. */
        WSASetLastError(WSAEMSGSIZE);
        return SOCKET_ERROR;
    }
    return static_cast<int>(copied);
}

/**
 * @brief Detour of recvfrom().
 *
 * A datagram which came from the relay of the association of the socket is
 * unwrapped, and the caller is answered with the address the payload was sent
 * to. A datagram which came from anywhere else, and every socket of a process
 * which has no association, is handed over the way the host delivers it.
 */
static int WSAAPI Hook_recvfrom(SOCKET s, char* buf, int len, int flags, sockaddr* from, int* fromlen)
{
    appbox::network::Proxy* proxy = appbox::network::ProxyOfProcess();
    if (proxy == nullptr || buf == nullptr || len < 0 || !proxy->HasAssociation(s))
    {
        return sys_recvfrom(s, buf, len, flags, from, fromlen);
    }

    /* The datagram is read with room for the header of the protocol. */
    std::vector<std::uint8_t>& scratch = appbox::network::IncomingBuffer();
    const std::size_t          capacity = static_cast<std::size_t>(len) + appbox::network::socks5::kMaxUdpHeaderSize;
    scratch.resize(capacity);

    sockaddr_storage source;
    int              source_length = sizeof(source);
    ZeroMemory(&source, sizeof(source));

    const int received = sys_recvfrom(s, reinterpret_cast<char*>(scratch.data()), static_cast<int>(capacity), flags,
                                      reinterpret_cast<sockaddr*>(&source), &source_length);
    if (received == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }

    const int family = reinterpret_cast<const sockaddr*>(&source)->sa_family;

    appbox::network::socks5::Endpoint sender;
    if (!appbox::network::EndpointFromSockaddr(reinterpret_cast<const sockaddr*>(&source), source_length, sender) ||
        !proxy->IsRelayOf(s, sender))
    {
        return Deliver(scratch.data(), static_cast<std::size_t>(received), buf, len, from, fromlen, sender, family);
    }

    appbox::network::socks5::Endpoint origin;
    const std::uint8_t*               payload = nullptr;
    std::size_t                       payload_size = 0;
    if (!appbox::network::socks5::ParseUdpDatagram(scratch.data(), static_cast<std::size_t>(received), origin, payload,
                                                   payload_size))
    {
        LOG_W("a datagram of the relay of the proxy is not a datagram of the protocol");
        return Deliver(nullptr, 0, buf, len, from, fromlen, sender, family);
    }

    return Deliver(payload, payload_size, buf, len, from, fromlen, origin, family);
}

static void LoadRecvFrom()
{
    sys_recvfrom = reinterpret_cast<T_recvfrom>(GetProcAddress(appbox::sys.h_ws2_32, "recvfrom"));
}

appbox::HookRecord appbox::HookRecvFrom = {
    "recvfrom",
    LoadRecvFrom,
    (void**)&sys_recvfrom,
    Hook_recvfrom,
};
