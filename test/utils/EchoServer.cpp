#include <winsock2.h> /* Must be first include file: it includes <windows.h> itself. */
#include <ws2tcpip.h>
#include "utils/EchoServer.hpp"
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace appbox::test
{
namespace
{

/** Timeout of a wait which has to notice the stop flag. */
constexpr DWORD kWaitTimeoutMs = 100;

/** Size of the buffer the server echoes through. */
constexpr std::size_t kBufferSize = 4096;

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
 * @brief Bind a socket to an ephemeral port of the loopback address.
 * @param[in] socket Socket to bind.
 * @param[in] type Type of the socket.
 * @param[out] port Port the socket was bound to.
 * @return true on success.
 */
bool BindLoopback(SOCKET socket, std::uint16_t& port)
{
    sockaddr_in address;
    ZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    int length = sizeof(address);
    if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR)
    {
        return false;
    }

    port = ntohs(address.sin_port);
    return true;
}

/**
 * @brief Format an address of the application.
 * @param[in] address The address to format.
 * @return The text of the address, `address:port`.
 */
std::string FormatAddress(const sockaddr_in& address)
{
    char text[INET_ADDRSTRLEN] = {};
    if (InetNtopA(AF_INET, &address.sin_addr, text, sizeof(text)) == nullptr)
    {
        return std::string();
    }
    return std::string(text) + ":" + std::to_string(ntohs(address.sin_port));
}

} // namespace

/**
 * @brief State of the echo server of a case.
 */
struct EchoServer::Impl
{
    /**
     * @brief Socket which accepts a connection.
     */
    SOCKET tcp_listener = INVALID_SOCKET;

    /**
     * @brief Socket which receives a datagram.
     */
    SOCKET udp_socket = INVALID_SOCKET;

    /**
     * @brief Port of the connection of the server.
     */
    std::uint16_t tcp_port = 0;

    /**
     * @brief Port of the datagram of the server.
     */
    std::uint16_t udp_port = 0;

    /**
     * @brief Thread which serves a connection.
     */
    std::thread tcp_thread;

    /**
     * @brief Thread which serves a datagram.
     */
    std::thread udp_thread;

    /**
     * @brief Whether the server has to stop.
     */
    std::atomic<bool> stop{ false };

    /**
     * @brief Protects the record of the server.
     */
    mutable std::mutex mutex;

    /**
     * @brief Whether a connection was accepted.
     */
    bool saw_tcp_connection = false;

    /**
     * @brief Address the last datagram came from.
     */
    std::string last_udp_peer;

    /**
     * @brief Accept one connection at a time and echo it back.
     */
    void ServeConnections();

    /**
     * @brief Receive a datagram and echo it back.
     */
    void ServeDatagrams();
};

void EchoServer::Impl::ServeConnections()
{
    while (!stop.load())
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(tcp_listener, &set);

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

        const SOCKET connection = accept(tcp_listener, nullptr, nullptr);
        if (connection == INVALID_SOCKET)
        {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            saw_tcp_connection = true;
        }

        char buffer[kBufferSize];
        for (;;)
        {
            const int read = recv(connection, buffer, sizeof(buffer), 0);
            if (read <= 0)
            {
                break;
            }

            int sent = 0;
            while (sent < read)
            {
                const int result = send(connection, buffer + sent, read - sent, 0);
                if (result <= 0)
                {
                    break;
                }
                sent += result;
            }
            if (sent < read)
            {
                break;
            }
        }

        closesocket(connection);
    }
}

void EchoServer::Impl::ServeDatagrams()
{
    const DWORD timeout = kWaitTimeoutMs;
    setsockopt(udp_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    char buffer[kBufferSize];
    while (!stop.load())
    {
        sockaddr_in from;
        int         from_length = sizeof(from);
        ZeroMemory(&from, sizeof(from));

        const int read =
            recvfrom(udp_socket, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &from_length);
        if (read == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAETIMEDOUT)
            {
                continue;
            }
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            last_udp_peer = FormatAddress(from);
        }

        sendto(udp_socket, buffer, read, 0, reinterpret_cast<const sockaddr*>(&from), from_length);
    }
}

EchoServer::EchoServer() : impl_(std::make_unique<Impl>())
{
}

EchoServer::~EchoServer()
{
    Stop();
}

bool EchoServer::Start()
{
    EnsureWinsock();

    impl_->stop.store(false);

    impl_->tcp_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    impl_->udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->tcp_listener == INVALID_SOCKET || impl_->udp_socket == INVALID_SOCKET)
    {
        Stop();
        return false;
    }

    if (!BindLoopback(impl_->tcp_listener, impl_->tcp_port) || !BindLoopback(impl_->udp_socket, impl_->udp_port) ||
        listen(impl_->tcp_listener, SOMAXCONN) == SOCKET_ERROR)
    {
        Stop();
        return false;
    }

    impl_->tcp_thread = std::thread([this]() { impl_->ServeConnections(); });
    impl_->udp_thread = std::thread([this]() { impl_->ServeDatagrams(); });
    return true;
}

void EchoServer::Stop()
{
    if (impl_ == nullptr)
    {
        return;
    }

    impl_->stop.store(true);

    if (impl_->tcp_listener != INVALID_SOCKET)
    {
        closesocket(impl_->tcp_listener);
        impl_->tcp_listener = INVALID_SOCKET;
    }
    if (impl_->udp_socket != INVALID_SOCKET)
    {
        closesocket(impl_->udp_socket);
        impl_->udp_socket = INVALID_SOCKET;
    }

    if (impl_->tcp_thread.joinable())
    {
        impl_->tcp_thread.join();
    }
    if (impl_->udp_thread.joinable())
    {
        impl_->udp_thread.join();
    }
}

std::uint16_t EchoServer::TcpPort() const
{
    return impl_ == nullptr ? 0 : impl_->tcp_port;
}

std::uint16_t EchoServer::UdpPort() const
{
    return impl_ == nullptr ? 0 : impl_->udp_port;
}

bool EchoServer::SawTcpConnection() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->saw_tcp_connection;
}

std::string EchoServer::LastUdpPeer() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->last_udp_peer;
}

} // namespace appbox::test
