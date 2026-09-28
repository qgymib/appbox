#include "Proxy.hpp"
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

namespace
{

/**
 * @brief Deadline of the whole handshake with the server of the proxy.
 *
 * A server which does not answer within the deadline fails the call of the
 * application instead of blocking it forever, which is the only failure mode
 * an application cannot work around.
 */
constexpr DWORD kHandshakeTimeoutMs = 30000;

/**
 * @brief Largest payload of a datagram which still fits a message.
 *
 * The largest datagram of the protocol of the internet carries 65507 bytes of
 * payload, and the header of the protocol takes up to 262 bytes of it.
 */
constexpr std::size_t kMaxDatagramPayload = 65507 - 262;

/**
 * @brief Largest number of the bytes which is sent or received at once.
 */
constexpr std::size_t kMaxChunk = 8192;

/**
 * @brief Buffer of the calling thread for the datagram which is sent.
 *
 * A datagram is carried by a buffer of the thread which sends it instead of a
 * buffer which is allocated for every datagram, because a datagram is small
 * and the allocation would dominate the cost of carrying it.
 *
 * @return The buffer of the calling thread.
 */
std::vector<std::uint8_t>& OutgoingBuffer()
{
    static thread_local std::vector<std::uint8_t> buffer;
    return buffer;
}

/**
 * @brief Translate a reply of the server into an error of winsock.
 * @param[in] reply Reply of the server.
 * @return The error which describes the reply.
 */
int ErrorOfReply(appbox::network::socks5::Reply reply)
{
    using appbox::network::socks5::Reply;

    switch (reply)
    {
    case Reply::NetworkUnreachable:
        return WSAENETUNREACH;
    case Reply::HostUnreachable:
        return WSAEHOSTUNREACH;
    case Reply::ConnectionRefused:
        return WSAECONNREFUSED;
    case Reply::TtlExpired:
        return WSAETIMEDOUT;
    case Reply::CommandNotSupported:
        return WSAEOPNOTSUPP;
    case Reply::AddressTypeNotSupported:
        return WSAEAFNOSUPPORT;
    case Reply::NotAllowed:
        return WSAEACCES;
    case Reply::Succeeded:
    case Reply::GeneralFailure:
        break;
    }
    return WSAECONNABORTED;
}

/**
 * @brief Build the address of a server out of the text of the configuration.
 *
 * @param[in] api Entry points of winsock.
 * @param[in] host Hostname or address literal of the server.
 * @param[in] port Port of the server.
 * @param[out] endpoint Address of the server as the protocol carries it.
 * @param[out] storage Address of the server as a socket needs it.
 * @param[out] length Number of the bytes of the address.
 * @return true when the address could be built.
 */
bool ServerAddress(const appbox::network::RawSocketApi& api, const std::string& host, std::uint16_t port,
                   appbox::network::socks5::Endpoint& endpoint, sockaddr_storage& storage, int& length)
{
    using appbox::network::socks5::AddressType;

    appbox::network_isolation::Address address;
    if (appbox::network_isolation::ParseAddress(host, address))
    {
        const bool ipv6 = address.family == appbox::network_isolation::AddressFamily::IPv6;
        endpoint.type = ipv6 ? AddressType::IPv6 : AddressType::IPv4;
        endpoint.host = appbox::network::socks5::FormatAddress(endpoint.type, address.bytes.data(), ipv6 ? 16u : 4u);
        endpoint.port = port;
        /* AF_UNSPEC is a macro, so it cannot be qualified by its namespace. */
        return appbox::network::SockaddrOfAddress(address, port, AF_UNSPEC, storage, length);
    }

    /*
     * The name of the server is resolved the way the application resolves a
     * name, so a redirection of the workspace applies to the server as well.
     */
    if (api.getaddrinfo == nullptr || api.freeaddrinfo == nullptr || host.empty())
    {
        return false;
    }

    ADDRINFOA hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const std::string service = std::to_string(static_cast<unsigned int>(port));
    PADDRINFOA        result = nullptr;
    if (api.getaddrinfo(host.c_str(), service.c_str(), &hints, &result) != 0)
    {
        return false;
    }

    bool found = false;
    for (PADDRINFOA item = result; item != nullptr && !found; item = item->ai_next)
    {
        if (item->ai_addr == nullptr || item->ai_addrlen < sizeof(sockaddr) ||
            item->ai_addrlen > sizeof(sockaddr_storage))
        {
            continue;
        }

        ZeroMemory(&storage, sizeof(storage));
        std::memcpy(&storage, item->ai_addr, item->ai_addrlen);
        length = static_cast<int>(item->ai_addrlen);
        found = appbox::network::EndpointFromSockaddr(reinterpret_cast<const sockaddr*>(&storage), length, endpoint);
    }
    api.freeaddrinfo(result);

    return found;
}

} // namespace

void appbox::network::Proxy::SetRawApi(const RawSocketApi& api)
{
    std::lock_guard<std::mutex> lock(mutex_);
    raw_ = api;
    UpdateFlagsLocked();
}

void appbox::network::Proxy::UpdateFlagsLocked()
{
    const bool installed = raw_.connect != nullptr && raw_.send != nullptr && raw_.recv != nullptr &&
                           raw_.socket != nullptr && raw_.select != nullptr && raw_.sendto != nullptr &&
                           raw_.closesocket != nullptr && raw_.getsockopt != nullptr;

    tcp_.store(installed && config_.tcp, std::memory_order_release);
    udp_.store(installed && config_.udp, std::memory_order_release);
}

void appbox::network::Proxy::Configure(const ProxyConfig& config)
{
    std::vector<SOCKET> controls;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (const auto& entry : sockets_)
        {
            if (entry.second.control != INVALID_SOCKET)
            {
                controls.push_back(entry.second.control);
            }
        }

        sockets_.clear();
        config_ = config;
        server_ = socks5::Endpoint{};
        server_resolved_ = false;
        UpdateFlagsLocked();
    }

    /* The connections of another server cannot be reused. */
    for (const SOCKET control : controls)
    {
        if (raw_.closesocket != nullptr)
        {
            raw_.closesocket(control);
        }
    }

    LOG_I("the proxy of the sandbox is {}", DescribeProxy(config));
}

void appbox::network::Proxy::Reset()
{
    std::map<SOCKET, SocketState> sockets;
    std::vector<SOCKET>           controls;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets.swap(sockets_);
        config_ = ProxyConfig{};
        server_ = socks5::Endpoint{};
        server_resolved_ = false;
        UpdateFlagsLocked();
    }

    for (const auto& entry : sockets)
    {
        if (entry.second.control != INVALID_SOCKET)
        {
            controls.push_back(entry.second.control);
        }
    }
    for (const SOCKET control : controls)
    {
        if (raw_.closesocket != nullptr)
        {
            raw_.closesocket(control);
        }
    }
}

bool appbox::network::Proxy::ProxiesTcp() const
{
    return tcp_.load(std::memory_order_acquire);
}

bool appbox::network::Proxy::ProxiesUdp() const
{
    return udp_.load(std::memory_order_acquire);
}

appbox::network::SocketKind appbox::network::Proxy::KindOf(SOCKET socket)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto                  it = sockets_.find(socket);
        if (it != sockets_.end() && it->second.kind_known)
        {
            return it->second.datagram ? SocketKind::Datagram : SocketKind::Stream;
        }
    }

    if (raw_.getsockopt == nullptr)
    {
        return SocketKind::Unknown;
    }

    int type = 0;
    int size = sizeof(type);
    if (raw_.getsockopt(socket, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &size) == SOCKET_ERROR)
    {
        return SocketKind::Unknown;
    }

    const bool datagram = type == SOCK_DGRAM;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SocketState&                state = sockets_[socket];
        state.kind_known = true;
        state.datagram = datagram;
    }

    return datagram ? SocketKind::Datagram : SocketKind::Stream;
}

bool appbox::network::Proxy::ProxiesDatagram(SOCKET socket)
{
    return ProxiesUdp() && KindOf(socket) == SocketKind::Datagram;
}

bool appbox::network::Proxy::HasAssociation(SOCKET socket) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto                  it = sockets_.find(socket);
    return it != sockets_.end() && it->second.associated;
}

bool appbox::network::Proxy::IsRelayOf(SOCKET socket, const socks5::Endpoint& source) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto                  it = sockets_.find(socket);
    if (it == sockets_.end() || !it->second.associated)
    {
        return false;
    }
    return socks5::EndpointsEqual(it->second.relay, source);
}

std::size_t appbox::network::Proxy::AssociationCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t                 count = 0;
    for (const auto& entry : sockets_)
    {
        if (entry.second.associated)
        {
            ++count;
        }
    }
    return count;
}

void appbox::network::Proxy::Close(SOCKET socket)
{
    SOCKET control = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto                  it = sockets_.find(socket);
        if (it == sockets_.end())
        {
            return;
        }

        control = it->second.control;
        sockets_.erase(it);
    }

    if (control != INVALID_SOCKET && raw_.closesocket != nullptr)
    {
        raw_.closesocket(control);
        LOG_D("the association of socket {} was closed with the socket", static_cast<unsigned long long>(socket));
    }
}

bool appbox::network::Proxy::ResolveServer(socks5::Endpoint& server, sockaddr_storage& storage, int& length)
{
    ProxyConfig config;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (server_resolved_)
        {
            server = server_;
            return SockaddrFromEndpoint(server_, AF_UNSPEC, storage, length);
        }
        config = config_;
    }

    socks5::Endpoint resolved;
    if (!ServerAddress(raw_, config.server, config.port, resolved, storage, length))
    {
        WSASetLastError(WSAEHOSTUNREACH);
        LOG_E("the server {} of the proxy could not be resolved", config.server);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        server_ = resolved;
        server_resolved_ = true;
        server = resolved;
    }
    return true;
}

bool appbox::network::Proxy::ConnectSocket(SOCKET socket, const sockaddr_storage& address, int length, DWORD deadline)
{
    if (raw_.connect(socket, reinterpret_cast<const sockaddr*>(&address), length) != SOCKET_ERROR)
    {
        return true;
    }

    const int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS && error != WSAEALREADY)
    {
        return false;
    }

    if (!WaitFor(socket, true, deadline))
    {
        return false;
    }

    int pending = 0;
    int size = sizeof(pending);
    if (raw_.getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&pending), &size) == SOCKET_ERROR)
    {
        return false;
    }
    if (pending != 0)
    {
        WSASetLastError(pending);
        return false;
    }
    return true;
}

bool appbox::network::Proxy::WaitFor(SOCKET socket, bool writable, DWORD deadline)
{
    for (;;)
    {
        const DWORD remaining = deadline - GetTickCount();
        if (remaining == 0 || remaining > kHandshakeTimeoutMs)
        {
            /* The deadline passed, or the tick count wrapped around it. */
            WSASetLastError(WSAETIMEDOUT);
            return false;
        }

        fd_set set;
        FD_ZERO(&set);
        FD_SET(socket, &set);

        timeval timeout;
        timeout.tv_sec = static_cast<long>(remaining / 1000);
        timeout.tv_usec = static_cast<long>((remaining % 1000) * 1000);

        const int result = raw_.select(0, writable ? nullptr : &set, writable ? &set : nullptr, nullptr, &timeout);
        if (result > 0)
        {
            return true;
        }
        if (result == SOCKET_ERROR)
        {
            return false;
        }
        /* The wait timed out, so the deadline is checked again. */
    }
}

bool appbox::network::Proxy::SendAll(SOCKET socket, const std::uint8_t* data, std::size_t size, DWORD deadline)
{
    std::size_t sent = 0;
    while (sent < size)
    {
        const std::size_t remaining = size - sent;
        const int         chunk = static_cast<int>(remaining < kMaxChunk ? remaining : kMaxChunk);
        const int         result = raw_.send(socket, reinterpret_cast<const char*>(data) + sent, chunk, 0);

        if (result > 0)
        {
            sent += static_cast<std::size_t>(result);
            continue;
        }
        if (result == 0)
        {
            WSASetLastError(WSAECONNRESET);
            return false;
        }
        if (WSAGetLastError() != WSAEWOULDBLOCK)
        {
            return false;
        }
        if (!WaitFor(socket, true, deadline))
        {
            return false;
        }
    }
    return true;
}

bool appbox::network::Proxy::RecvAll(SOCKET socket, std::uint8_t* data, std::size_t size, DWORD deadline)
{
    std::size_t received = 0;
    while (received < size)
    {
        const std::size_t remaining = size - received;
        const int         chunk = static_cast<int>(remaining < kMaxChunk ? remaining : kMaxChunk);
        const int         result = raw_.recv(socket, reinterpret_cast<char*>(data) + received, chunk, 0);

        if (result > 0)
        {
            received += static_cast<std::size_t>(result);
            continue;
        }
        if (result == 0)
        {
            /* The server closed the connection in the middle of the handshake. */
            WSASetLastError(WSAECONNRESET);
            return false;
        }
        if (WSAGetLastError() != WSAEWOULDBLOCK)
        {
            return false;
        }
        if (!WaitFor(socket, false, deadline))
        {
            return false;
        }
    }
    return true;
}

bool appbox::network::Proxy::Handshake(SOCKET socket, socks5::Command command, const socks5::Endpoint& target,
                                       socks5::Endpoint& bound, DWORD deadline)
{
    const bool with_credentials = config_.HasCredentials();

    /* The greeting offers the methods the configuration can use. */
    {
        const std::vector<std::uint8_t> greeting = socks5::BuildGreeting(with_credentials);
        if (!SendAll(socket, greeting.data(), greeting.size(), deadline))
        {
            return false;
        }

        std::uint8_t answer[2] = {};
        if (!RecvAll(socket, answer, sizeof(answer), deadline))
        {
            return false;
        }

        socks5::Method method = socks5::Method::NoAcceptable;
        if (!socks5::ParseMethodSelection(answer, sizeof(answer), method))
        {
            WSASetLastError(WSAEPROTONOSUPPORT);
            LOG_E("the server of the proxy answered a greeting which is not SOCKS5");
            return false;
        }

        if (method == socks5::Method::UserPassword)
        {
            const std::vector<std::uint8_t> request =
                socks5::BuildUserPasswordRequest(config_.username, config_.password);
            if (request.empty())
            {
                WSASetLastError(WSAEACCES);
                LOG_E("the credentials of the proxy do not fit a request");
                return false;
            }
            if (!SendAll(socket, request.data(), request.size(), deadline))
            {
                return false;
            }

            std::uint8_t reply[2] = {};
            if (!RecvAll(socket, reply, sizeof(reply), deadline) ||
                !socks5::ParseUserPasswordReply(reply, sizeof(reply)))
            {
                WSASetLastError(WSAEACCES);
                LOG_E("the server of the proxy refused the credentials");
                return false;
            }
        }
        else if (method != socks5::Method::NoAuthentication)
        {
            WSASetLastError(WSAEPROTONOSUPPORT);
            LOG_E("the server of the proxy selected an authentication method which is not supported");
            return false;
        }
    }

    /* The request names the target, and its answer names the address of the relay. */
    const std::vector<std::uint8_t> request = socks5::BuildRequest(command, target);
    if (request.empty())
    {
        WSASetLastError(WSAEAFNOSUPPORT);
        LOG_E("the target {} of the proxy cannot be sent", target.host);
        return false;
    }
    if (!SendAll(socket, request.data(), request.size(), deadline))
    {
        return false;
    }

    std::vector<std::uint8_t> message(socks5::kHeaderSize, 0);
    if (!RecvAll(socket, message.data(), message.size(), deadline))
    {
        return false;
    }
    if (message[0] != socks5::kVersion)
    {
        WSASetLastError(WSAEPROTONOSUPPORT);
        return false;
    }

    const auto type = static_cast<socks5::AddressType>(message[3]);
    if (type == socks5::AddressType::DomainName)
    {
        std::uint8_t length = 0;
        if (!RecvAll(socket, &length, 1, deadline))
        {
            return false;
        }
        message.push_back(length);

        const std::size_t begin = message.size();
        message.resize(begin + length);
        if (!RecvAll(socket, message.data() + begin, length, deadline))
        {
            return false;
        }
    }
    else
    {
        const std::size_t address_size = socks5::AddressSize(type);
        if (address_size == 0)
        {
            WSASetLastError(WSAEPROTONOSUPPORT);
            return false;
        }

        const std::size_t begin = message.size();
        message.resize(begin + address_size);
        if (!RecvAll(socket, message.data() + begin, address_size, deadline))
        {
            return false;
        }
    }

    const std::size_t port_begin = message.size();
    message.resize(port_begin + 2);
    if (!RecvAll(socket, message.data() + port_begin, 2, deadline))
    {
        return false;
    }

    socks5::Reply    reply = socks5::Reply::GeneralFailure;
    socks5::Endpoint address;
    if (!socks5::ParseReply(message.data(), message.size(), reply, address))
    {
        WSASetLastError(WSAEPROTONOSUPPORT);
        return false;
    }
    if (reply != socks5::Reply::Succeeded)
    {
        WSASetLastError(ErrorOfReply(reply));
        LOG_W("the server of the proxy refused the request with the reply {}", static_cast<unsigned int>(reply));
        return false;
    }

    bound = address;
    return true;
}

int appbox::network::Proxy::ConnectTcp(SOCKET socket, const socks5::Endpoint& target)
{
    const DWORD deadline = GetTickCount() + kHandshakeTimeoutMs;

    socks5::Endpoint server;
    sockaddr_storage storage;
    int              length = 0;
    ZeroMemory(&storage, sizeof(storage));

    if (!ResolveServer(server, storage, length))
    {
        return SOCKET_ERROR;
    }

    /*
     * A connection to the server itself is not carried by the server, which
     * would ask it to connect to itself.
     */
    if (socks5::EndpointsEqual(server, target))
    {
        LOG_D("the connection to {} is the server of the proxy itself", target.host);
        if (raw_.connect(socket, reinterpret_cast<const sockaddr*>(&storage), length) == SOCKET_ERROR)
        {
            return SOCKET_ERROR;
        }
        return 0;
    }

    if (!ConnectSocket(socket, storage, length, deadline))
    {
        LOG_W("the connection to the server {} of the proxy failed", server.host);
        return SOCKET_ERROR;
    }

    socks5::Endpoint bound;
    if (!Handshake(socket, socks5::Command::Connect, target, bound, deadline))
    {
        return SOCKET_ERROR;
    }

    LOG_D("the connection to {}:{} is carried by the proxy", target.host, target.port);
    return 0;
}

bool appbox::network::Proxy::OpenAssociation(SOCKET socket, socks5::Endpoint& relay)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto                  it = sockets_.find(socket);
        if (it != sockets_.end() && it->second.associated)
        {
            relay = it->second.relay;
            return true;
        }
    }

    const DWORD deadline = GetTickCount() + kHandshakeTimeoutMs;

    socks5::Endpoint server;
    sockaddr_storage storage;
    int              length = 0;
    ZeroMemory(&storage, sizeof(storage));

    if (!ResolveServer(server, storage, length))
    {
        return false;
    }

    const SOCKET control =
        raw_.socket(reinterpret_cast<const sockaddr*>(&storage)->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (control == INVALID_SOCKET)
    {
        return false;
    }

    /*
     * The address the client sends from is not known before the socket is
     * bound, and the protocol allows an association to be asked for with the
     * unspecified address.
     */
    socks5::Endpoint client;
    client.type = socks5::AddressType::IPv4;
    client.host = "0.0.0.0";
    client.port = 0;

    socks5::Endpoint bound;
    const bool       opened = ConnectSocket(control, storage, length, deadline) &&
                              Handshake(control, socks5::Command::UdpAssociate, client, bound, deadline);
    if (!opened)
    {
        raw_.closesocket(control);
        return false;
    }

    /*
     * A server which reports the unspecified address asks the client to keep
     * the address it reached the server with.
     */
    if (bound.host == "0.0.0.0" || bound.host == "::")
    {
        bound.host = server.host;
        bound.type = server.type;
    }
    if (bound.port == 0)
    {
        bound.port = server.port;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        SocketState&                state = sockets_[socket];
        if (state.associated)
        {
            /* Another thread opened an association of this socket first. */
            relay = state.relay;
            raw_.closesocket(control);
            return true;
        }

        state.associated = true;
        state.control = control;
        state.relay = bound;
        relay = bound;
    }

    LOG_D("the datagrams of socket {} are carried by the relay {}:{}", static_cast<unsigned long long>(socket),
          bound.host, bound.port);
    return true;
}

int appbox::network::Proxy::SendDatagram(SOCKET socket, const socks5::Endpoint& target, const WSABUF* buffers,
                                         std::size_t count)
{
    if (buffers == nullptr || count == 0)
    {
        WSASetLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    std::size_t payload_size = 0;
    for (std::size_t index = 0; index < count; ++index)
    {
        if (buffers[index].buf == nullptr)
        {
            WSASetLastError(WSAEFAULT);
            return SOCKET_ERROR;
        }
        payload_size += buffers[index].len;
    }
    if (payload_size > kMaxDatagramPayload)
    {
        WSASetLastError(WSAEMSGSIZE);
        return SOCKET_ERROR;
    }

    socks5::Endpoint relay;
    if (!OpenAssociation(socket, relay))
    {
        return SOCKET_ERROR;
    }

    std::vector<std::uint8_t>& datagram = OutgoingBuffer();
    datagram = socks5::BuildUdpDatagram(target, nullptr, 0);
    if (datagram.empty())
    {
        WSASetLastError(WSAEAFNOSUPPORT);
        return SOCKET_ERROR;
    }

    datagram.reserve(datagram.size() + payload_size);
    for (std::size_t index = 0; index < count; ++index)
    {
        const WSABUF& buffer = buffers[index];
        datagram.insert(datagram.end(), buffer.buf, buffer.buf + buffer.len);
    }

    sockaddr_storage storage;
    int              length = 0;
    ZeroMemory(&storage, sizeof(storage));
    if (!SockaddrFromEndpoint(relay, AF_UNSPEC, storage, length))
    {
        WSASetLastError(WSAEAFNOSUPPORT);
        return SOCKET_ERROR;
    }

    const int sent =
        raw_.sendto(socket, reinterpret_cast<const char*>(datagram.data()), static_cast<int>(datagram.size()), 0,
                    reinterpret_cast<const sockaddr*>(&storage), length);
    if (sent == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }

    /* The caller expects the number of the payload bytes it handed over. */
    return static_cast<int>(payload_size);
}
