#include <winsock2.h> /* Must be first include file: it includes <windows.h> itself. */
#include <ws2tcpip.h>
#include "utils/Socks5Server.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace appbox::test
{
namespace
{

/** Version of the protocol. */
constexpr std::uint8_t kVersion = 0x05;

/** Version of the user name and password authentication. */
constexpr std::uint8_t kAuthVersion = 0x01;

/** Command which opens a connection to a target. */
constexpr std::uint8_t kCommandConnect = 0x01;

/** Command which opens a relay for the datagrams of the client. */
constexpr std::uint8_t kCommandUdpAssociate = 0x03;

/** Reply which reports a failure of the server. */
constexpr std::uint8_t kReplyGeneralFailure = 0x01;

/** Reply which reports that the command is not supported. */
constexpr std::uint8_t kReplyCommandNotSupported = 0x07;

/** Timeout of a wait which has to notice the stop flag. */
constexpr DWORD kWaitTimeoutMs = 100;

/** Size of the buffer a stream relay copies through. */
constexpr std::size_t kRelayBufferSize = 4096;

/** Size of the buffer a datagram relay receives into. */
constexpr std::size_t kDatagramBufferSize = 65535;

/**
 * @brief Initialize the socket library of the test process.
 */
void EnsureWinsock()
{
    static const bool initialized = []() {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)initialized;
}

/**
 * @brief Send every byte of a buffer.
 * @param[in] socket Socket to send with.
 * @param[in] data Bytes to send.
 * @param[in] size Number of the bytes to send.
 * @return true when every byte was sent.
 */
bool SendAll(SOCKET socket, const void* data, std::size_t size)
{
    const char* bytes = static_cast<const char*>(data);
    std::size_t sent = 0;
    while (sent < size)
    {
        const int result = send(socket, bytes + sent, static_cast<int>(size - sent), 0);
        if (result <= 0)
        {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

/**
 * @brief Receive every byte of a buffer.
 * @param[in] socket Socket to receive from.
 * @param[out] data Buffer of the message.
 * @param[in] size Number of the bytes to receive.
 * @return true when every byte was received.
 */
bool RecvAll(SOCKET socket, void* data, std::size_t size)
{
    char*       bytes = static_cast<char*>(data);
    std::size_t received = 0;
    while (received < size)
    {
        const int result = recv(socket, bytes + received, static_cast<int>(size - received), 0);
        if (result <= 0)
        {
            return false;
        }
        received += static_cast<std::size_t>(result);
    }
    return true;
}

/**
 * @brief Read the address of a message of the protocol.
 *
 * @param[in] data Bytes of the message.
 * @param[in] size Number of the bytes which are available.
 * @param[in] offset Position of the type of the address.
 * @param[out] host Text of the address.
 * @param[out] port Port of the address.
 * @param[out] next Position of the first byte after the address.
 * @return true when the whole address is available.
 */
bool ReadAddress(const std::uint8_t* data, std::size_t size, std::size_t offset, std::string& host, std::uint16_t& port,
                 std::size_t& next)
{
    if (offset >= size)
    {
        return false;
    }

    const std::uint8_t type = data[offset];
    ++offset;

    if (type == 0x01)
    {
        if (size - offset < 4)
        {
            return false;
        }
        char    text[INET_ADDRSTRLEN] = {};
        in_addr address;
        std::memcpy(&address, data + offset, 4);
        if (InetNtopA(AF_INET, &address, text, sizeof(text)) == nullptr)
        {
            return false;
        }
        host = text;
        offset += 4;
    }
    else if (type == 0x04)
    {
        if (size - offset < 16)
        {
            return false;
        }
        char     text[INET6_ADDRSTRLEN] = {};
        in6_addr address;
        std::memcpy(&address, data + offset, 16);
        if (InetNtopA(AF_INET6, &address, text, sizeof(text)) == nullptr)
        {
            return false;
        }
        host = text;
        offset += 16;
    }
    else if (type == 0x03)
    {
        if (offset >= size)
        {
            return false;
        }
        const std::size_t length = data[offset];
        ++offset;
        if (length == 0 || size - offset < length)
        {
            return false;
        }
        host.assign(reinterpret_cast<const char*>(data + offset), length);
        offset += length;
    }
    else
    {
        return false;
    }

    if (size - offset < 2)
    {
        return false;
    }
    port = static_cast<std::uint16_t>((data[offset] << 8) | data[offset + 1]);
    offset += 2;

    next = offset;
    return true;
}

/**
 * @brief Build a reply of the server.
 * @param[in] reply Reply code.
 * @param[in] bound Address the reply reports.
 * @return The bytes of the reply.
 */
std::vector<std::uint8_t> BuildReply(std::uint8_t reply, const sockaddr_in& bound)
{
    std::vector<std::uint8_t> message;
    message.push_back(kVersion);
    message.push_back(reply);
    message.push_back(0x00);
    message.push_back(0x01);

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&bound.sin_addr);
    message.insert(message.end(), bytes, bytes + 4);

    const std::uint16_t port = ntohs(bound.sin_port);
    message.push_back(static_cast<std::uint8_t>(port >> 8));
    message.push_back(static_cast<std::uint8_t>(port & 0xff));
    return message;
}

} // namespace

/**
 * @brief State of the SOCKS5 server of a case.
 */
struct Socks5Server::Impl
{
    /**
     * @brief Socket the server accepts on.
     */
    SOCKET listener = INVALID_SOCKET;

    /**
     * @brief Port the server listens on.
     */
    std::uint16_t port = 0;

    /**
     * @brief Thread which accepts the connections.
     */
    std::thread accept_thread;

    /**
     * @brief Threads which serve one connection each.
     */
    std::vector<std::thread> workers;

    /**
     * @brief Whether the server has to stop.
     */
    std::atomic<bool> stop{ false };

    /**
     * @brief User name the server expects.
     */
    std::string username;

    /**
     * @brief Password the server expects.
     */
    std::string password;

    /**
     * @brief Protects the record of the server.
     */
    mutable std::mutex mutex;

    /**
     * @brief Requests the server received.
     */
    std::vector<Socks5Request> requests;

    /**
     * @brief Whether a client performed the credential exchange.
     */
    bool saw_credentials = false;

    /**
     * @brief Whether a request was refused.
     */
    bool saw_refusal = false;

    /**
     * @brief Port of the relay of the last association.
     */
    std::uint16_t udp_relay_port = 0;

    /**
     * @brief Sockets which serve a connection, so Stop() can release them.
     */
    std::vector<SOCKET> connections;

    /**
     * @brief Whether the server asks for the credentials.
     * @return true when a credential is configured.
     */
    bool WantsCredentials() const
    {
        return !username.empty() || !password.empty();
    }

    /**
     * @brief Relay the bytes of a connection until one side closes.
     * @param[in] control Connection to the client.
     * @param[in] target Connection to the target.
     */
    void RelayStream(SOCKET control, SOCKET target);

    /**
     * @brief Relay the datagrams of an association.
     *
     * The relay socket carries both directions, which is how the protocol names
     * it: a datagram which comes from the client is unwrapped and forwarded to
     * the target it names, and a datagram which comes from a target is wrapped
     * and sent back to the client.
     *
     * @param[in] control Connection the association belongs to.
     * @param[in] relay Socket of the relay.
     */
    void RelayDatagrams(SOCKET control, SOCKET relay);

    /**
     * @brief Answer a request with a failure.
     * @param[in] control Connection to the client.
     * @param[in] reply Reply code.
     */
    void Refuse(SOCKET control, std::uint8_t reply);

    /**
     * @brief Serve one connection of a client.
     * @param[in] control Connection to the client.
     */
    void ServeConnection(SOCKET control);

    /**
     * @brief Accept the connections of the clients until the server stops.
     */
    void AcceptConnections();
};

void Socks5Server::Impl::RelayStream(SOCKET control, SOCKET target)
{
    char buffer[kRelayBufferSize];
    while (!stop.load())
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(control, &set);
        FD_SET(target, &set);

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = kWaitTimeoutMs * 1000;

        const int ready = select(0, &set, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR)
        {
            return;
        }
        if (ready == 0)
        {
            continue;
        }

        if (FD_ISSET(control, &set))
        {
            const int read = recv(control, buffer, sizeof(buffer), 0);
            if (read <= 0 || !SendAll(target, buffer, static_cast<std::size_t>(read)))
            {
                return;
            }
        }
        if (FD_ISSET(target, &set))
        {
            const int read = recv(target, buffer, sizeof(buffer), 0);
            if (read <= 0 || !SendAll(control, buffer, static_cast<std::size_t>(read)))
            {
                return;
            }
        }
    }
}

void Socks5Server::Impl::RelayDatagrams(SOCKET control, SOCKET relay)
{
    const DWORD timeout = kWaitTimeoutMs;
    setsockopt(relay, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    std::vector<std::uint8_t> buffer(kDatagramBufferSize);
    sockaddr_in               client;
    ZeroMemory(&client, sizeof(client));
    bool has_client = false;

    while (!stop.load())
    {
        /* A control connection which was closed ends the association. */
        fd_set control_set;
        FD_ZERO(&control_set);
        FD_SET(control, &control_set);
        timeval poll;
        poll.tv_sec = 0;
        poll.tv_usec = 0;
        if (select(0, &control_set, nullptr, nullptr, &poll) > 0)
        {
            char probe = 0;
            if (recv(control, &probe, 1, 0) <= 0)
            {
                return;
            }
        }

        sockaddr_in from;
        int         from_length = sizeof(from);
        ZeroMemory(&from, sizeof(from));

        const int read = recvfrom(relay, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                                  reinterpret_cast<sockaddr*>(&from), &from_length);
        if (read == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAETIMEDOUT)
            {
                continue;
            }
            return;
        }

        const bool from_client =
            !has_client || (from.sin_addr.s_addr == client.sin_addr.s_addr && from.sin_port == client.sin_port);

        if (from_client)
        {
            std::string   host;
            std::uint16_t target_port = 0;
            std::size_t   next = 0;
            if (!ReadAddress(buffer.data(), static_cast<std::size_t>(read), 3, host, target_port, next))
            {
                continue;
            }

            sockaddr_in target;
            ZeroMemory(&target, sizeof(target));
            target.sin_family = AF_INET;
            target.sin_port = htons(target_port);
            if (InetPtonA(AF_INET, host.c_str(), &target.sin_addr) != 1)
            {
                continue;
            }

            client = from;
            has_client = true;

            sendto(relay, reinterpret_cast<const char*>(buffer.data()) + next, read - static_cast<int>(next), 0,
                   reinterpret_cast<const sockaddr*>(&target), sizeof(target));
            continue;
        }

        /* A datagram of a target: it is wrapped and sent back to the client. */
        std::vector<std::uint8_t> datagram;
        datagram.push_back(0x00);
        datagram.push_back(0x00);
        datagram.push_back(0x00);
        datagram.push_back(0x01);

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&from.sin_addr);
        datagram.insert(datagram.end(), bytes, bytes + 4);

        const std::uint16_t sender_port = ntohs(from.sin_port);
        datagram.push_back(static_cast<std::uint8_t>(sender_port >> 8));
        datagram.push_back(static_cast<std::uint8_t>(sender_port & 0xff));
        datagram.insert(datagram.end(), buffer.begin(), buffer.begin() + read);

        sendto(relay, reinterpret_cast<const char*>(datagram.data()), static_cast<int>(datagram.size()), 0,
               reinterpret_cast<const sockaddr*>(&client), sizeof(client));
    }
}

void Socks5Server::Impl::Refuse(SOCKET control, std::uint8_t reply)
{
    sockaddr_in bound;
    ZeroMemory(&bound, sizeof(bound));

    const std::vector<std::uint8_t> message = BuildReply(reply, bound);
    SendAll(control, message.data(), message.size());
}

void Socks5Server::Impl::ServeConnection(SOCKET control)
{
    /* The greeting offers the methods of the client. */
    std::uint8_t head[2] = {};
    if (!RecvAll(control, head, sizeof(head)) || head[0] != kVersion)
    {
        return;
    }

    std::vector<std::uint8_t> methods(head[1]);
    if (!methods.empty() && !RecvAll(control, methods.data(), methods.size()))
    {
        return;
    }

    const bool   wants_credentials = WantsCredentials();
    std::uint8_t selected = wants_credentials ? 0x02 : 0x00;
    if (wants_credentials && std::find(methods.begin(), methods.end(), 0x02) == methods.end())
    {
        selected = 0xFF;
    }

    const std::uint8_t answer[2] = { kVersion, selected };
    if (!SendAll(control, answer, sizeof(answer)) || selected == 0xFF)
    {
        return;
    }

    if (selected == 0x02)
    {
        std::uint8_t credential[2] = {};
        if (!RecvAll(control, credential, sizeof(credential)) || credential[0] != kAuthVersion)
        {
            return;
        }

        std::string user(credential[1], '\0');
        if (!user.empty() && !RecvAll(control, user.data(), user.size()))
        {
            return;
        }

        std::uint8_t password_length = 0;
        if (!RecvAll(control, &password_length, 1))
        {
            return;
        }

        std::string offered_password(password_length, '\0');
        if (!offered_password.empty() && !RecvAll(control, offered_password.data(), offered_password.size()))
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            saw_credentials = true;
        }

        const bool         accepted = user == username && password == password;
        const std::uint8_t reply[2] = { kAuthVersion, static_cast<std::uint8_t>(accepted ? 0x00 : 0x01) };
        if (!SendAll(control, reply, sizeof(reply)) || !accepted)
        {
            return;
        }
    }

    /* The request names the target. */
    std::uint8_t request_head[4] = {};
    if (!RecvAll(control, request_head, sizeof(request_head)) || request_head[0] != kVersion)
    {
        return;
    }

    std::vector<std::uint8_t> message(request_head, request_head + sizeof(request_head));
    std::size_t               address_length = 0;
    if (request_head[3] == 0x01)
    {
        address_length = 4;
    }
    else if (request_head[3] == 0x04)
    {
        address_length = 16;
    }
    else if (request_head[3] == 0x03)
    {
        std::uint8_t length = 0;
        if (!RecvAll(control, &length, 1))
        {
            return;
        }
        message.push_back(length);
        address_length = length;
    }
    else
    {
        return;
    }

    const std::size_t address_begin = message.size();
    message.resize(address_begin + address_length);
    if (address_length != 0 && !RecvAll(control, message.data() + address_begin, address_length))
    {
        return;
    }

    const std::size_t port_begin = message.size();
    message.resize(port_begin + 2);
    if (!RecvAll(control, message.data() + port_begin, 2))
    {
        return;
    }

    std::string   host;
    std::uint16_t target_port = 0;
    std::size_t   next = 0;
    if (!ReadAddress(message.data(), message.size(), 3, host, target_port, next))
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        Socks5Request               request;
        request.command = request_head[1];
        request.address = host;
        request.port = target_port;
        requests.push_back(request);
    }

    if (request_head[1] == kCommandConnect)
    {
        addrinfo hints;
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo*         result = nullptr;
        const std::string service = std::to_string(target_port);
        if (getaddrinfo(host.c_str(), service.c_str(), &hints, &result) != 0 || result == nullptr)
        {
            Refuse(control, kReplyGeneralFailure);
            return;
        }

        const SOCKET target = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        const bool   connected =
            target != INVALID_SOCKET && connect(target, result->ai_addr, static_cast<int>(result->ai_addrlen)) == 0;
        freeaddrinfo(result);

        if (!connected)
        {
            if (target != INVALID_SOCKET)
            {
                closesocket(target);
            }
            Refuse(control, kReplyGeneralFailure);
            return;
        }

        sockaddr_in bound;
        int         bound_length = sizeof(bound);
        ZeroMemory(&bound, sizeof(bound));
        getsockname(target, reinterpret_cast<sockaddr*>(&bound), &bound_length);

        const std::vector<std::uint8_t> reply = BuildReply(0x00, bound);
        if (!SendAll(control, reply.data(), reply.size()))
        {
            closesocket(target);
            return;
        }

        RelayStream(control, target);
        closesocket(target);
        return;
    }

    if (request_head[1] == kCommandUdpAssociate)
    {
        const SOCKET relay = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (relay == INVALID_SOCKET)
        {
            Refuse(control, kReplyGeneralFailure);
            return;
        }

        sockaddr_in address;
        ZeroMemory(&address, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        int length = sizeof(address);
        if (bind(relay, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
            getsockname(relay, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR)
        {
            closesocket(relay);
            Refuse(control, kReplyGeneralFailure);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            udp_relay_port = ntohs(address.sin_port);
        }

        const std::vector<std::uint8_t> reply = BuildReply(0x00, address);
        if (!SendAll(control, reply.data(), reply.size()))
        {
            closesocket(relay);
            return;
        }

        RelayDatagrams(control, relay);
        closesocket(relay);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        saw_refusal = true;
    }
    Refuse(control, kReplyCommandNotSupported);
}

void Socks5Server::Impl::AcceptConnections()
{
    while (!stop.load())
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(listener, &set);

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = kWaitTimeoutMs * 1000;

        const int ready = select(0, &set, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR)
        {
            return;
        }
        if (ready == 0)
        {
            continue;
        }

        const SOCKET control = accept(listener, nullptr, nullptr);
        if (control == INVALID_SOCKET)
        {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            connections.push_back(control);
        }

        workers.emplace_back([this, control]() { ServeConnection(control); });
    }
}

Socks5Server::Socks5Server() : impl_(std::make_unique<Impl>())
{
}

Socks5Server::~Socks5Server()
{
    Stop();
}

bool Socks5Server::Start(const std::string& username, const std::string& password)
{
    EnsureWinsock();

    impl_->username = username;
    impl_->password = password;
    impl_->stop.store(false);

    impl_->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->listener == INVALID_SOCKET)
    {
        return false;
    }

    sockaddr_in address;
    ZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    int length = sizeof(address);
    if (bind(impl_->listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(impl_->listener, SOMAXCONN) == SOCKET_ERROR ||
        getsockname(impl_->listener, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR)
    {
        closesocket(impl_->listener);
        impl_->listener = INVALID_SOCKET;
        return false;
    }

    impl_->port = ntohs(address.sin_port);
    impl_->accept_thread = std::thread([this]() { impl_->AcceptConnections(); });
    return true;
}

void Socks5Server::Stop()
{
    if (impl_ == nullptr)
    {
        return;
    }

    impl_->stop.store(true);

    if (impl_->listener != INVALID_SOCKET)
    {
        closesocket(impl_->listener);
        impl_->listener = INVALID_SOCKET;
    }

    /* The accept thread is joined first, so no worker is added afterwards. */
    if (impl_->accept_thread.joinable())
    {
        impl_->accept_thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        for (const SOCKET control : impl_->connections)
        {
            closesocket(control);
        }
        impl_->connections.clear();
    }

    for (auto& worker : impl_->workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    impl_->workers.clear();
}

std::uint16_t Socks5Server::Port() const
{
    return impl_ == nullptr ? 0 : impl_->port;
}

std::uint16_t Socks5Server::UdpRelayPort() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->udp_relay_port;
}

bool Socks5Server::SawCredentials() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->saw_credentials;
}

bool Socks5Server::SawRefusal() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->saw_refusal;
}

std::vector<Socks5Request> Socks5Server::Requests() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->requests;
}

} // namespace appbox::test
