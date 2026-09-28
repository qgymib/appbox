#ifndef APPBOX_SANDBOX_NETWORK_PROXY_HPP
#define APPBOX_SANDBOX_NETWORK_PROXY_HPP

#include "utils/Winsock.hpp"
#include "ProxyConfig.hpp"
#include "Socks5.hpp"
#include <atomic>
#include <map>
#include <mutex>

namespace appbox
{
namespace network
{

/**
 * @brief Entry points of winsock the proxy reaches its server with.
 *
 * The proxy opens its own connections to the SOCKS5 server, so it has to call
 * the original entry points of the process instead of the ones the sandbox
 * hooks: a call through the hooked entry point would be carried by the proxy
 * again and would recurse. The hooks resolve the entry points and install them
 * here once they are attached, which is the moment the saved pointers carry the
 * trampoline to the original code.
 */
struct RawSocketApi
{
    /**
     * @brief Original connect(), used to reach the server and its relay.
     */
    int(WSAAPI* connect)(SOCKET, const sockaddr*, int) = nullptr;

    /**
     * @brief Original sendto(), used to reach the relay of an association.
     */
    int(WSAAPI* sendto)(SOCKET, const char*, int, int, const sockaddr*, int) = nullptr;

    /**
     * @brief Original closesocket(), used to release a control connection.
     */
    int(WSAAPI* closesocket)(SOCKET) = nullptr;

    /**
     * @brief Original send(), used by the handshake.
     */
    int(WSAAPI* send)(SOCKET, const char*, int, int) = nullptr;

    /**
     * @brief Original recv(), used by the handshake.
     */
    int(WSAAPI* recv)(SOCKET, char*, int, int) = nullptr;

    /**
     * @brief Original getsockopt(), used to read the kind of a socket.
     */
    int(WSAAPI* getsockopt)(SOCKET, int, int, char*, int*) = nullptr;

    /**
     * @brief Original socket(), used to open a control connection.
     */
    SOCKET(WSAAPI* socket)(int, int, int) = nullptr;

    /**
     * @brief Original select(), used to wait for a socket which is not ready.
     */
    int(WSAAPI* select)(int, fd_set*, fd_set*, fd_set*, const timeval*) = nullptr;

    /**
     * @brief Original getaddrinfo(), used to resolve the server of the proxy.
     */
    int(WSAAPI* getaddrinfo)(PCSTR, PCSTR, const ADDRINFOA*, PADDRINFOA*) = nullptr;

    /**
     * @brief Original freeaddrinfo(), used to release a name resolution.
     */
    void(WSAAPI* freeaddrinfo)(PADDRINFOA) = nullptr;
};

/**
 * @brief Kind of a socket of the application.
 */
enum class SocketKind
{
    Unknown, ///< The kind of the socket could not be read.
    Stream,  ///< The socket carries a byte stream, which the TCP proxy takes.
    Datagram ///< The socket carries datagrams, which the UDP proxy takes.
};

/**
 * @brief SOCKS5 proxy of a sandboxed process.
 *
 * The engine carries the traffic of the application through the server of the
 * network isolation file: a TCP connection is established by the handshake of
 * `ConnectTcp` before the call of the application returns, and a datagram is
 * wrapped by `SendDatagram` and unwrapped by the hook which receives it.
 *
 * The engine never changes the mode of a socket and never changes the address
 * it is bound to: the handshake waits for a socket which is not ready yet with
 * `select`, so a socket of an application which works without a blocking call
 * keeps working the same way with the proxy.
 *
 * A socket which is not ready and whose handshake times out fails the call
 * with `WSAETIMEDOUT`, and a server which refuses a request fails it with the
 * error of the reply: a connection is never carried directly once the proxy is
 * enabled, so the application can never believe that it is proxied while it is
 * not.
 *
 * The engine holds one association per datagram socket (see `SendDatagram`),
 * so the calls of a socket are serialized by its own state and the calls of
 * two sockets do not wait for each other.
 */
class Proxy
{
public:
    Proxy() = default;
    Proxy(const Proxy&) = delete;
    Proxy& operator=(const Proxy&) = delete;

    /**
     * @brief Install the entry points the proxy reaches its server with.
     *
     * The call is made by the hook layer once the hooks are attached. The
     * proxy carries no traffic before it, because a call through an entry
     * point which is not installed yet cannot be made safely.
     *
     * @param[in] api The entry points of winsock.
     */
    void SetRawApi(const RawSocketApi& api);

    /**
     * @brief Apply the proxy of the network isolation file.
     *
     * The call replaces the configuration of the engine and drops every
     * association which is open, because the associations of another server
     * cannot be reused.
     *
     * @param[in] config The proxy of the isolation file.
     */
    void Configure(const ProxyConfig& config);

    /**
     * @brief Drop the configuration and every association.
     */
    void Reset();

    /**
     * @brief Whether the proxy carries the TCP traffic.
     * @return true when TCP is configured and the entry points are installed.
     */
    bool ProxiesTcp() const;

    /**
     * @brief Whether the proxy carries the UDP traffic.
     * @return true when UDP is configured and the entry points are installed.
     */
    bool ProxiesUdp() const;

    /**
     * @brief Read the kind of a socket.
     *
     * The kind of a socket never changes, so it is read once and kept until
     * the socket is closed: the call is made on the path of every datagram.
     *
     * @param[in] socket The socket to read.
     * @return The kind of the socket.
     */
    SocketKind KindOf(SOCKET socket);

    /**
     * @brief Whether the proxy carries the datagrams of a socket.
     * @param[in] socket The socket to inspect.
     * @return true when UDP is configured and the socket is a datagram socket.
     */
    bool ProxiesDatagram(SOCKET socket);

    /**
     * @brief Connect a socket to a target through the proxy.
     *
     * The call connects the socket to the server of the proxy and performs the
     * handshake of the protocol, so the socket is connected to the target when
     * the call returns.
     *
     * @param[in] socket The socket to connect.
     * @param[in] target Address and port the application asked for.
     * @return 0 on success, `SOCKET_ERROR` on failure with the last error set.
     */
    int ConnectTcp(SOCKET socket, const socks5::Endpoint& target);

    /**
     * @brief Whether a socket has an association.
     * @param[in] socket The socket to inspect.
     * @return true when the socket sends its datagrams through the proxy.
     */
    bool HasAssociation(SOCKET socket) const;

    /**
     * @brief Whether an address is the relay of the association of a socket.
     * @param[in] socket The socket to inspect.
     * @param[in] source Address a datagram came from.
     * @return true when the datagram was sent by the relay of the socket.
     */
    bool IsRelayOf(SOCKET socket, const socks5::Endpoint& source) const;

    /**
     * @brief Send the payload of a datagram through the association.
     *
     * The association of the socket is opened by the first datagram it sends
     * (see `OpenAssociation`) and is reused by every datagram which follows,
     * so a socket which talks to several targets carries them all through the
     * one relay the server granted it.
     *
     * @param[in] socket The socket to send with.
     * @param[in] target Address and port the payload has to reach.
     * @param[in] buffers Payload of the datagram.
     * @param[in] count Number of the buffers of the payload.
     * @return The number of the payload bytes which were sent, which is what
     *         the caller of a datagram call expects, or `SOCKET_ERROR` on
     *         failure with the last error set.
     */
    int SendDatagram(SOCKET socket, const socks5::Endpoint& target, const WSABUF* buffers, std::size_t count);

    /**
     * @brief Drop the state of a socket the application closed.
     *
     * The control connection of an association is closed with the socket which
     * owns it, so a socket which is closed does not leave a connection behind.
     *
     * @param[in] socket The socket which is being closed.
     */
    void Close(SOCKET socket);

    /**
     * @brief Number of the associations which are open.
     * @return The number of the sockets which send their datagrams through the
     *         proxy.
     */
    std::size_t AssociationCount() const;

private:
    /**
     * @brief State of one socket of the application.
     */
    struct SocketState
    {
        /**
         * @brief Whether the kind of the socket was read.
         */
        bool kind_known = false;

        /**
         * @brief Whether the socket carries datagrams.
         */
        bool datagram = false;

        /**
         * @brief Whether the socket has an association.
         */
        bool associated = false;

        /**
         * @brief Control connection of the association, invalid while none is open.
         */
        SOCKET control = INVALID_SOCKET;

        /**
         * @brief Relay the datagrams of the association are sent to.
         */
        socks5::Endpoint relay;
    };

    /**
     * @brief Recompute the flags the hooks read from the configuration.
     *
     * The proxy carries traffic only while its configuration asks for it and
     * every entry point it calls is installed, so the call is made by the two
     * places which change either of them.
     *
     * @note The caller holds the lock of the engine.
     */
    void UpdateFlagsLocked();

    /**
     * @brief Read the address of the server of the proxy.
     *
     * The address of the server is the one of the isolation file: an address
     * literal is used as it is, and a name is resolved the way the application
     * resolves a name, so a DNS redirection of the workspace applies to it as
     * well. The result is kept for the run, because the configuration of the
     * proxy does not change while it carries traffic.
     *
     * @param[out] server Address of the server as the protocol carries it.
     * @param[out] storage Address of the server as a socket needs it.
     * @param[out] length Number of the bytes of the address.
     * @return true on success, otherwise false with the last error set.
     */
    bool ResolveServer(socks5::Endpoint& server, sockaddr_storage& storage, int& length);

    /**
     * @brief Connect a socket to the address of the server.
     *
     * The call waits for a connection which the stack is still establishing,
     * so a socket of an application which does not block works the same way.
     *
     * @param[in] socket The socket to connect.
     * @param[in] address Address of the server.
     * @param[in] length Number of the bytes of the address.
     * @param[in] deadline Tick count the call has to finish before.
     * @return true on success, otherwise false with the last error set.
     */
    bool ConnectSocket(SOCKET socket, const sockaddr_storage& address, int length, DWORD deadline);

    /**
     * @brief Open the association of a socket.
     *
     * The association is opened once per socket: the call reports the relay of
     * the association which is already open, or opens one and keeps it for the
     * datagrams which follow.
     *
     * @param[in] socket The socket to open an association for.
     * @param[out] relay The relay of the association.
     * @return true on success, otherwise false with the last error set.
     */
    bool OpenAssociation(SOCKET socket, socks5::Endpoint& relay);

    /**
     * @brief Perform the handshake of the protocol on a socket.
     *
     * @param[in] socket Socket which is connected to the server.
     * @param[in] command Command of the request.
     * @param[in] target Address and port the request names.
     * @param[out] bound Address and port the server reports.
     * @param[in] deadline Tick count the handshake has to finish before.
     * @return true on success, otherwise false with the last error set.
     */
    bool Handshake(SOCKET socket, socks5::Command command, const socks5::Endpoint& target, socks5::Endpoint& bound,
                   DWORD deadline);

    /**
     * @brief Send a whole message of the protocol.
     * @param[in] socket Socket to send with.
     * @param[in] data Bytes to send.
     * @param[in] size Number of the bytes to send.
     * @param[in] deadline Tick count the call has to finish before.
     * @return true when every byte was sent.
     */
    bool SendAll(SOCKET socket, const std::uint8_t* data, std::size_t size, DWORD deadline);

    /**
     * @brief Receive a whole message of the protocol.
     * @param[in] socket Socket to receive from.
     * @param[out] data Buffer of the message.
     * @param[in] size Number of the bytes to receive.
     * @param[in] deadline Tick count the call has to finish before.
     * @return true when every byte was received.
     */
    bool RecvAll(SOCKET socket, std::uint8_t* data, std::size_t size, DWORD deadline);

    /**
     * @brief Wait until a socket is ready for an operation.
     * @param[in] socket Socket to wait for.
     * @param[in] writable Whether the socket is waited for writing.
     * @param[in] deadline Tick count the wait has to finish before.
     * @return true when the socket is ready, false on a failure or a timeout
     *         with the last error set.
     */
    bool WaitFor(SOCKET socket, bool writable, DWORD deadline);

    /**
     * @brief The entry points the proxy reaches its server with.
     */
    RawSocketApi raw_;

    /**
     * @brief The configuration of the proxy.
     */
    ProxyConfig config_;

    /**
     * @brief Address of the server of the proxy, empty while it is not resolved.
     */
    socks5::Endpoint server_;

    /**
     * @brief Whether the address of the server was resolved.
     */
    bool server_resolved_ = false;

    /**
     * @brief Whether the proxy carries the TCP traffic.
     *
     * The flag is the one the hooks read on their hot path: it is set by the
     * installation of the entry points, which is the moment the proxy becomes
     * usable.
     */
    std::atomic<bool> tcp_{ false };

    /**
     * @brief Whether the proxy carries the UDP traffic.
     */
    std::atomic<bool> udp_{ false };

    /**
     * @brief Protects the configuration and the state of the sockets.
     */
    mutable std::mutex mutex_;

    /**
     * @brief State of the sockets, by handle.
     */
    std::map<SOCKET, SocketState> sockets_;
};

} // namespace network
} // namespace appbox

#endif // APPBOX_SANDBOX_NETWORK_PROXY_HPP
