#ifndef APPBOX_SANDBOX_NETWORK_PROXYCONFIG_HPP
#define APPBOX_SANDBOX_NETWORK_PROXYCONFIG_HPP

#include <cstdint>
#include <string>

namespace appbox
{
namespace network
{

/**
 * @brief Proxy of the network isolation file.
 *
 * The configuration is the `proxy` member of the network isolation file (see
 * `common/NetworkIsolation.hpp` for the schema), which the packer writes while
 * the `Proxy` page of the `Network` workspace holds one. The two flags are the
 * switch of the proxy: at least one of them has to be set for the traffic of
 * the application to be carried by the server.
 *
 * The structure holds no Windows dependency, so the rules of the file are unit
 * testable.
 */
struct ProxyConfig
{
    /**
     * @brief Whether the TCP traffic of the application is proxied.
     */
    bool tcp = false;

    /**
     * @brief Whether the UDP traffic of the application is proxied.
     */
    bool udp = false;

    /**
     * @brief Hostname or address of the proxy server, in UTF-8.
     */
    std::string server;

    /**
     * @brief Port of the proxy server, in host byte order.
     */
    std::uint16_t port = 0;

    /**
     * @brief Optional user name, empty while no authentication is configured.
     */
    std::string username;

    /**
     * @brief Optional password, empty while no authentication is configured.
     */
    std::string password;

    /**
     * @brief Whether the proxy carries traffic.
     * @return true when at least one of the two protocols is proxied.
     */
    bool IsEnabled() const
    {
        return tcp || udp;
    }

    /**
     * @brief Whether the proxy carries credentials.
     * @return true when the user name or the password carries a value.
     */
    bool HasCredentials() const
    {
        return !username.empty() || !password.empty();
    }
};

/**
 * @brief Read the proxy of a network isolation file.
 *
 * The call is the reader of the `proxy` member of the file, which the packer
 * writes next to the DNS redirections. A document which is not a network
 * isolation file of the supported version is rejected; a document of that
 * version which carries no `proxy` member, which names another protocol or
 * whose server and port cannot be used describes a session without a proxy, so
 * the call reports a disabled configuration instead of failing: the sandbox
 * then lets the application connect the way it does without an isolation file.
 *
 * @param[in] text Text of the isolation file.
 * @param[out] out The proxy of the file, disabled when the file holds none.
 * @return true when the document is a network isolation file of the supported
 *         version.
 */
bool ParseProxyConfig(const std::string& text, ProxyConfig& out);

/**
 * @brief Describe a proxy configuration for a log message.
 *
 * The description names the protocol, the protocols which are proxied and the
 * server with its port, and never the credentials: a log line is written
 * without leaking a password.
 *
 * @param[in] config The configuration to describe.
 * @return The description, `"none"` for a configuration which carries no
 *         traffic.
 */
std::string DescribeProxy(const ProxyConfig& config);

} // namespace network
} // namespace appbox

#endif // APPBOX_SANDBOX_NETWORK_PROXYCONFIG_HPP
