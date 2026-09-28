#ifndef APPBOX_TEST_UTILS_ECHOSERVER_HPP
#define APPBOX_TEST_UTILS_ECHOSERVER_HPP

#include <cstdint>
#include <memory>
#include <string>

namespace appbox::test
{

/**
 * @brief Echo server of the end to end cases of the proxy.
 *
 * The server listens on the loopback address for a connection and for a
 * datagram, echoes back what it receives and keeps the address of the last
 * datagram it received: a case which reads that address pins which address the
 * datagram came from, which is the way it sees whether the traffic was carried
 * by the relay of the proxy.
 *
 * The server is a helper of the test process and never part of the product.
 */
class EchoServer
{
public:
    EchoServer();
    ~EchoServer();

    EchoServer(const EchoServer&) = delete;
    EchoServer& operator=(const EchoServer&) = delete;

    /**
     * @brief Start the server on the loopback address with ephemeral ports.
     * @return true on success.
     */
    bool Start();

    /**
     * @brief Stop the server and release every socket.
     */
    void Stop();

    /**
     * @brief Port the connection of the server listens on.
     * @return The port, zero while the server is not started.
     */
    std::uint16_t TcpPort() const;

    /**
     * @brief Port the datagram of the server listens on.
     * @return The port, zero while the server is not started.
     */
    std::uint16_t UdpPort() const;

    /**
     * @brief Whether a connection was accepted.
     * @return true when the server accepted at least one connection.
     */
    bool SawTcpConnection() const;

    /**
     * @brief Address the last datagram came from.
     * @return The address as `address:port`, empty while no datagram arrived.
     */
    std::string LastUdpPeer() const;

private:
    struct Impl;

    /**
     * @brief State of the server.
     */
    std::unique_ptr<Impl> impl_;
};

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_ECHOSERVER_HPP
