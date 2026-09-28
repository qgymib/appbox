#ifndef APPBOX_TEST_UTILS_SOCKS5SERVER_HPP
#define APPBOX_TEST_UTILS_SOCKS5SERVER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief One request a SOCKS5 server of a case received.
 */
struct Socks5Request
{
    /**
     * @brief Command of the request, one for CONNECT and three for UDP ASSOCIATE.
     */
    std::uint8_t command = 0;

    /**
     * @brief Address the request names, as the request carries it.
     */
    std::string address;

    /**
     * @brief Port the request names.
     */
    std::uint16_t port = 0;
};

/**
 * @brief Minimal SOCKS5 server of the end to end cases of the proxy.
 *
 * The server listens on the loopback address, performs the handshake of the
 * protocol with the client and answers its requests: a CONNECT is established
 * against the target and relayed, and a UDP ASSOCIATE opens a relay which wraps
 * and unwraps the datagrams of the client. Every request it received is kept,
 * so a case can pin what the sandbox asked for.
 *
 * The server is a helper of the test process and never part of the product: it
 * only implements what a case needs.
 */
class Socks5Server
{
public:
    Socks5Server();
    ~Socks5Server();

    Socks5Server(const Socks5Server&) = delete;
    Socks5Server& operator=(const Socks5Server&) = delete;

    /**
     * @brief Start the server on the loopback address with an ephemeral port.
     *
     * A credential which is not empty makes the server ask for the user name
     * and password method and refuse every other one.
     *
     * @param[in] username User name the server expects, empty for no authentication.
     * @param[in] password Password the server expects.
     * @return true on success.
     */
    bool Start(const std::string& username, const std::string& password);

    /**
     * @brief Stop the server and release every connection.
     */
    void Stop();

    /**
     * @brief Port the server listens on.
     * @return The port, zero while the server is not started.
     */
    std::uint16_t Port() const;

    /**
     * @brief Port of the relay of the last association.
     * @return The port, zero while no association was opened.
     */
    std::uint16_t UdpRelayPort() const;

    /**
     * @brief Whether the server was asked for its credentials.
     * @return true when a client performed the user name and password exchange.
     */
    bool SawCredentials() const;

    /**
     * @brief Whether the server refused a request.
     * @return true when a request was answered with a failure.
     */
    bool SawRefusal() const;

    /**
     * @brief Every request the server received.
     * @return The requests in the order they were received.
     */
    std::vector<Socks5Request> Requests() const;

private:
    struct Impl;

    /**
     * @brief State of the server.
     */
    std::unique_ptr<Impl> impl_;
};

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_SOCKS5SERVER_HPP
