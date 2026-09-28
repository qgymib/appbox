#ifndef APPBOX_SANDBOX_UTILS_PROXYHOOK_HPP
#define APPBOX_SANDBOX_UTILS_PROXYHOOK_HPP

#include "utils/Winsock.hpp"
#include "NetworkIsolation.hpp"
#include "network/Proxy.hpp"
#include <vector>

namespace appbox
{
namespace network
{

/**
 * @brief Helpers the hooks of the proxy share.
 *
 * The hooks of the socket API live one per file below `hook/`, and every one of
 * them has to translate the addresses of the application into the addresses of
 * the protocol and to reach the engine of the sandbox. Those two jobs are not
 * the job of a single hook, so they live here.
 */

/**
 * @brief Translate an address of the application into an endpoint of the protocol.
 *
 * Only the two address families of the protocol are translated: an address of
 * another family cannot be carried by a request, so the caller forwards the
 * call of the application instead of proxying it.
 *
 * @param[in] address Address of the application, may be null.
 * @param[in] length Number of the bytes of the address.
 * @param[out] out The endpoint of the address, untouched on failure.
 * @return true when the address was translated.
 */
bool EndpointFromSockaddr(const sockaddr* address, int length, socks5::Endpoint& out);

/**
 * @brief Build an address of the application out of a parsed address.
 *
 * An IPv4 address is reported to an application which works with IPv6 sockets
 * as its mapped form, so a socket of either family can report the address a
 * datagram came from.
 *
 * @param[in] address Parsed address.
 * @param[in] port Port of the address, in host byte order.
 * @param[in] family Family the application works with, `AF_UNSPEC` to use the
 *                   family of the address itself.
 * @param[out] storage Address of the application, untouched on failure.
 * @param[out] length Number of the bytes of the address, untouched on failure.
 * @return true when the address could be built.
 */
bool SockaddrOfAddress(const network_isolation::Address& address, std::uint16_t port, int family,
                       sockaddr_storage& storage, int& length);

/**
 * @brief Build an address of the application out of an endpoint of the protocol.
 *
 * @param[in] endpoint Endpoint of the protocol.
 * @param[in] family Family the application works with, `AF_UNSPEC` to use the
 *                   family of the endpoint itself.
 * @param[out] storage Address of the application, untouched on failure.
 * @param[out] length Number of the bytes of the address, untouched on failure.
 * @return true when the address could be built.
 */
bool SockaddrFromEndpoint(const socks5::Endpoint& endpoint, int family, sockaddr_storage& storage, int& length);

/**
 * @brief Write an address of the protocol into the buffer of the caller.
 *
 * A caller of a receive call reports the address of the sender in a buffer
 * whose size it hands over, so the address is only written while it fits and
 * the size is replaced by the size of the address either way.
 *
 * @param[in] endpoint Address of the protocol.
 * @param[in] family Family of the address the caller works with.
 * @param[out] address Buffer of the caller, may be null.
 * @param[in,out] length Size of the buffer, replaced by the size of the address.
 * @return true when the address was written.
 */
bool WriteAddress(const socks5::Endpoint& endpoint, int family, sockaddr* address, int* length);

/**
 * @brief Buffer of the calling thread for a datagram which is received.
 *
 * A datagram of an association is read into a buffer which has room for the
 * header of the protocol as well, so the hooks of the receive path share one
 * buffer per thread instead of allocating one for every datagram.
 *
 * @return The buffer of the calling thread.
 */
std::vector<std::uint8_t>& IncomingBuffer();

/**
 * @brief The proxy of the sandbox process.
 * @return The engine, null when the process has no sandbox instance.
 */
Proxy* ProxyOfProcess();

/**
 * @brief Install the entry points the proxy reaches its server with.
 *
 * The call is made by the hook layer once the hooks are attached, which is the
 * moment the saved entry points carry the trampoline to the original code: a
 * pointer which was saved before the attach would point at the hooked code and
 * would make the proxy carry its own traffic.
 */
void InstallRawSocketApi();

} // namespace network
} // namespace appbox

#endif // APPBOX_SANDBOX_UTILS_PROXYHOOK_HPP
