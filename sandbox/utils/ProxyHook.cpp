#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/ProxyHook.hpp"
#include "utils/Log.hpp"
#include "hook/closesocket.hpp"
#include "hook/connect.hpp"
#include "hook/sendto.hpp"
#include "Sandbox.hpp"
#include <cstring>

namespace
{

/**
 * @brief Whether a family is the one of the protocol of the internet.
 * @param[in] family Family of an address.
 * @return true when the family is IPv4 or IPv6.
 */
bool IsInternetFamily(int family)
{
    return family == AF_INET || family == AF_INET6;
}

/**
 * @brief Build the address of an IPv4 socket.
 * @param[in] address The four bytes of the address, in network order.
 * @param[in] port Port of the address, in host byte order.
 * @param[out] storage Address of the application.
 * @param[out] length Number of the bytes of the address.
 */
void BuildIpv4(const std::uint8_t* address, std::uint16_t port, sockaddr_storage& storage, int& length)
{
    sockaddr_in ipv4;
    ZeroMemory(&ipv4, sizeof(ipv4));
    ipv4.sin_family = AF_INET;
    ipv4.sin_port = htons(port);
    std::memcpy(&ipv4.sin_addr, address, 4);

    ZeroMemory(&storage, sizeof(storage));
    std::memcpy(&storage, &ipv4, sizeof(ipv4));
    length = static_cast<int>(sizeof(ipv4));
}

/**
 * @brief Build the address of an IPv6 socket.
 * @param[in] address The sixteen bytes of the address, in network order.
 * @param[in] port Port of the address, in host byte order.
 * @param[out] storage Address of the application.
 * @param[out] length Number of the bytes of the address.
 */
void BuildIpv6(const std::uint8_t* address, std::uint16_t port, sockaddr_storage& storage, int& length)
{
    sockaddr_in6 ipv6;
    ZeroMemory(&ipv6, sizeof(ipv6));
    ipv6.sin6_family = AF_INET6;
    ipv6.sin6_port = htons(port);
    std::memcpy(&ipv6.sin6_addr, address, 16);

    ZeroMemory(&storage, sizeof(storage));
    std::memcpy(&storage, &ipv6, sizeof(ipv6));
    length = static_cast<int>(sizeof(ipv6));
}

} // namespace

bool appbox::network::EndpointFromSockaddr(const sockaddr* address, int length, socks5::Endpoint& out)
{
    if (address == nullptr || length < static_cast<int>(sizeof(sockaddr)))
    {
        return false;
    }

    if (address->sa_family == AF_INET)
    {
        if (length < static_cast<int>(sizeof(sockaddr_in)))
        {
            return false;
        }

        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
        out.type = socks5::AddressType::IPv4;
        out.host = socks5::FormatAddress(out.type, reinterpret_cast<const std::uint8_t*>(&ipv4->sin_addr), 4);
        out.port = ntohs(ipv4->sin_port);
        return true;
    }

    if (address->sa_family == AF_INET6)
    {
        if (length < static_cast<int>(sizeof(sockaddr_in6)))
        {
            return false;
        }

        const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address);
        out.type = socks5::AddressType::IPv6;
        out.host = socks5::FormatAddress(out.type, ipv6->sin6_addr.u.Byte, 16);
        out.port = ntohs(ipv6->sin6_port);
        return true;
    }

    return false;
}

bool appbox::network::SockaddrOfAddress(const network_isolation::Address& address, std::uint16_t port, int family,
                                        sockaddr_storage& storage, int& length)
{
    if (family != AF_UNSPEC && !IsInternetFamily(family))
    {
        return false;
    }

    if (address.family == network_isolation::AddressFamily::IPv4)
    {
        /*
         * A socket which works with IPv6 addresses reports an IPv4 address as
         * its mapped form, which is how the stack names it as well.
         */
        if (family == AF_INET6)
        {
            std::uint8_t mapped[16] = {};
            mapped[10] = 0xff;
            mapped[11] = 0xff;
            std::memcpy(mapped + 12, address.bytes.data(), 4);
            BuildIpv6(mapped, port, storage, length);
            return true;
        }

        BuildIpv4(address.bytes.data(), port, storage, length);
        return true;
    }

    if (family == AF_INET)
    {
        /* An IPv6 address cannot be reported to an IPv4 socket. */
        return false;
    }

    BuildIpv6(address.bytes.data(), port, storage, length);
    return true;
}

bool appbox::network::SockaddrFromEndpoint(const socks5::Endpoint& endpoint, int family, sockaddr_storage& storage,
                                           int& length)
{
    network_isolation::Address address;
    if (!network_isolation::ParseAddress(endpoint.host, address))
    {
        /*
         * A domain name is never the address of a socket: the sandbox resolves
         * a name before it builds an address, and the protocol carries a name
         * only inside a message.
         */
        return false;
    }

    return SockaddrOfAddress(address, endpoint.port, family, storage, length);
}

bool appbox::network::WriteAddress(const socks5::Endpoint& endpoint, int family, sockaddr* address, int* length)
{
    if (address == nullptr || length == nullptr)
    {
        return false;
    }

    sockaddr_storage storage;
    int              written = 0;
    ZeroMemory(&storage, sizeof(storage));
    if (!SockaddrFromEndpoint(endpoint, family, storage, written))
    {
        return false;
    }

    const int         available = *length;
    const std::size_t room = available > 0 ? static_cast<std::size_t>(available) : 0;
    const std::size_t copy = room < sizeof(storage) ? room : sizeof(storage);
    if (copy != 0)
    {
        std::memcpy(address, &storage, copy);
    }

    *length = written;
    return true;
}

std::vector<std::uint8_t>& appbox::network::IncomingBuffer()
{
    static thread_local std::vector<std::uint8_t> buffer;
    return buffer;
}

appbox::network::Proxy* appbox::network::ProxyOfProcess()
{
    if (appbox::sandbox == nullptr)
    {
        return nullptr;
    }
    return appbox::sandbox->proxy.get();
}

void appbox::network::InstallRawSocketApi()
{
    Proxy* proxy = ProxyOfProcess();
    if (proxy == nullptr)
    {
        return;
    }

    HMODULE ws2_32 = appbox::sys.h_ws2_32;
    if (ws2_32 == nullptr)
    {
        LOG_E("the module of the socket API is not loaded");
        return;
    }

    RawSocketApi api;
    api.connect = sys_connect;
    api.sendto = sys_sendto;
    api.closesocket = sys_closesocket;
    api.send = reinterpret_cast<int(WSAAPI*)(SOCKET, const char*, int, int)>(GetProcAddress(ws2_32, "send"));
    api.recv = reinterpret_cast<int(WSAAPI*)(SOCKET, char*, int, int)>(GetProcAddress(ws2_32, "recv"));
    api.getsockopt =
        reinterpret_cast<int(WSAAPI*)(SOCKET, int, int, char*, int*)>(GetProcAddress(ws2_32, "getsockopt"));
    api.socket = reinterpret_cast<SOCKET(WSAAPI*)(int, int, int)>(GetProcAddress(ws2_32, "socket"));
    api.select = reinterpret_cast<int(WSAAPI*)(int, fd_set*, fd_set*, fd_set*, const timeval*)>(
        GetProcAddress(ws2_32, "select"));
    api.getaddrinfo = reinterpret_cast<int(WSAAPI*)(PCSTR, PCSTR, const ADDRINFOA*, PADDRINFOA*)>(
        GetProcAddress(ws2_32, "getaddrinfo"));
    api.freeaddrinfo = reinterpret_cast<void(WSAAPI*)(PADDRINFOA)>(GetProcAddress(ws2_32, "freeaddrinfo"));

    proxy->SetRawApi(api);
}
