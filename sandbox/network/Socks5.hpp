#ifndef APPBOX_SANDBOX_NETWORK_SOCKS5_HPP
#define APPBOX_SANDBOX_NETWORK_SOCKS5_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace appbox
{
namespace network
{
namespace socks5
{

/**
 * @brief Codec of the SOCKS5 protocol (RFC 1928) and of its user name and
 * password authentication (RFC 1929).
 *
 * The module holds no Windows dependency: it turns the messages of the
 * protocol into bytes and the bytes of an answer back into their meaning, so
 * the rules of the protocol are unit testable without a socket.
 *
 * Every message of the protocol starts with its version, and a reply of the
 * server carries a variable length address, so a reader which is handed a
 * partial message reports that it needs more bytes instead of guessing.
 */

/**
 * @brief Version of the protocol.
 */
inline constexpr std::uint8_t kVersion = 0x05;

/**
 * @brief Version of the user name and password authentication.
 */
inline constexpr std::uint8_t kAuthVersion = 0x01;

/**
 * @brief Length of the header of a request and of a reply.
 *
 * The header holds the version, the command or the reply code, a reserved byte
 * and the type of the address which follows it.
 */
inline constexpr std::size_t kHeaderSize = 4;

/**
 * @brief Length of the header of a UDP datagram.
 *
 * The header holds two reserved bytes, the fragment number and the type of the
 * address of the datagram.
 */
inline constexpr std::size_t kUdpHeaderSize = 4;

/**
 * @brief Longest domain name a request can carry.
 *
 * The length of the name is a single byte of the message.
 */
inline constexpr std::size_t kMaxDomainNameLength = 255;

/**
 * @brief Longest user name and password a request can carry.
 *
 * The length of each credential is a single byte of the message.
 */
inline constexpr std::size_t kMaxCredentialLength = 255;

/**
 * @brief Largest header a datagram of an association can carry.
 *
 * The header holds the four leading bytes, the counted length of a domain name
 * and the two bytes of the port, so a caller which reads a datagram into a
 * buffer has to add the constant to the room the payload needs.
 */
inline constexpr std::size_t kMaxUdpHeaderSize = kUdpHeaderSize + 1 + kMaxDomainNameLength + 2;

/**
 * @brief Authentication method the client offers and the server selects.
 */
enum class Method : std::uint8_t
{
    NoAuthentication = 0x00, ///< The server needs no credential.
    UserPassword = 0x02,     ///< The server asks for the credentials of RFC 1929.
    NoAcceptable = 0xFF      ///< The server accepts none of the offered methods.
};

/**
 * @brief Command of a request.
 */
enum class Command : std::uint8_t
{
    Connect = 0x01,     ///< Open a TCP connection to the target.
    UdpAssociate = 0x03 ///< Open a UDP relay for the client.
};

/**
 * @brief Type of the address of a request, of a reply or of a datagram.
 */
enum class AddressType : std::uint8_t
{
    IPv4 = 0x01,       ///< The address is four bytes.
    DomainName = 0x03, ///< The address is a counted name.
    IPv6 = 0x04        ///< The address is sixteen bytes.
};

/**
 * @brief Reply code of the server.
 */
enum class Reply : std::uint8_t
{
    Succeeded = 0x00,              ///< The request was granted.
    GeneralFailure = 0x01,         ///< The server reported a failure of its own.
    NotAllowed = 0x02,             ///< The rules of the server forbid the request.
    NetworkUnreachable = 0x03,     ///< The network of the target is unreachable.
    HostUnreachable = 0x04,        ///< The host of the target is unreachable.
    ConnectionRefused = 0x05,      ///< The target refused the connection.
    TtlExpired = 0x06,             ///< The time to live of the request expired.
    CommandNotSupported = 0x07,    ///< The server does not know the command.
    AddressTypeNotSupported = 0x08 ///< The server does not know the address type.
};

/**
 * @brief One address of the protocol together with its port.
 *
 * The `host` is the text of the address: an IPv4 or an IPv6 literal for the
 * two literal types and the name itself for a domain name. The `type` is set
 * by the readers of the module; a builder derives the type of a message from
 * the text of the host, so a caller which fills an endpoint by hand only has
 * to fill the host and the port.
 */
struct Endpoint
{
    /**
     * @brief Type of the address.
     */
    AddressType type = AddressType::IPv4;

    /**
     * @brief Text of the address, in UTF-8 for a domain name.
     */
    std::string host;

    /**
     * @brief Port of the address, in host byte order.
     */
    std::uint16_t port = 0;
};

/**
 * @brief Whether two endpoints name the same address and port.
 *
 * The text of the two hosts is compared the way the protocol carries it: the
 * two readers of the module render an address the same way, so a comparison of
 * the text is a comparison of the address.
 *
 * @param[in] left Left endpoint.
 * @param[in] right Right endpoint.
 * @return true when both name the same address and port.
 */
bool EndpointsEqual(const Endpoint& left, const Endpoint& right);

/**
 * @brief Render an address of the protocol as text.
 *
 * An IPv4 address is rendered as a dotted quad and an IPv6 address in its
 * compressed form, which is the text the name resolution of the operating
 * system reads back as the same address.
 *
 * @param[in] type Type of the address.
 * @param[in] bytes Bytes of the address, four or sixteen of them.
 * @param[in] size Number of the bytes which are available.
 * @return The text of the address, empty when the type is unknown or the bytes
 *         are missing.
 */
std::string FormatAddress(AddressType type, const std::uint8_t* bytes, std::size_t size);

/**
 * @brief Build the greeting which offers the authentication methods.
 * @param[in] withCredentials Whether the user name and password method is
 *                            offered as well.
 * @return The bytes of the greeting.
 */
std::vector<std::uint8_t> BuildGreeting(bool withCredentials);

/**
 * @brief Read the method the server selected.
 * @param[in] data Bytes of the answer of the server.
 * @param[in] size Number of the bytes which are available.
 * @param[out] method The selected method, untouched on failure.
 * @return true when the answer is complete and holds a method.
 */
bool ParseMethodSelection(const std::uint8_t* data, std::size_t size, Method& method);

/**
 * @brief Build the user name and password request of RFC 1929.
 *
 * A credential which is longer than the single byte which carries its length
 * cannot be sent; the call reports an empty message instead of a truncated one.
 *
 * @param[in] user User name to send.
 * @param[in] password Password to send.
 * @return The bytes of the request, empty when a credential is too long.
 */
std::vector<std::uint8_t> BuildUserPasswordRequest(std::string_view user, std::string_view password);

/**
 * @brief Read the answer of the user name and password request.
 * @param[in] data Bytes of the answer of the server.
 * @param[in] size Number of the bytes which are available.
 * @return true when the answer is complete and the credentials were accepted.
 */
bool ParseUserPasswordReply(const std::uint8_t* data, std::size_t size);

/**
 * @brief Build a request of the protocol.
 *
 * The type of the address of the request is derived from the host of the
 * target: an IPv4 literal is sent as four bytes, an IPv6 literal as sixteen,
 * and every other text as a domain name, which the server resolves itself. A
 * domain name which is longer than a single byte can carry cannot be sent, and
 * the call reports an empty message then.
 *
 * @param[in] command Command of the request.
 * @param[in] target Address and port the request names.
 * @return The bytes of the request, empty when the target cannot be sent.
 */
std::vector<std::uint8_t> BuildRequest(Command command, const Endpoint& target);

/**
 * @brief Read a reply of the server.
 *
 * @param[in] data Bytes of the reply.
 * @param[in] size Number of the bytes which are available.
 * @param[out] reply The reply code, untouched on failure.
 * @param[out] bound The address and the port the server reports, untouched on
 *                   failure. For an association the address is the relay the
 *                   client sends its datagrams to; a relay of the unspecified
 *                   address means the client keeps the address it used.
 * @return true when the whole reply is available.
 */
bool ParseReply(const std::uint8_t* data, std::size_t size, Reply& reply, Endpoint& bound);

/**
 * @brief Number of the bytes the header of a reply or of a datagram needs to
 *        report the length of its address.
 *
 * The caller of a reader which is handed a partial message can use the call to
 * learn how many bytes it has to read before the message can be read.
 *
 * @param[in] type Type of the address.
 * @return The number of the bytes of the address, zero when the type is not
 *         known.
 */
std::size_t AddressSize(AddressType type);

/**
 * @brief Build the datagram of an association.
 *
 * The datagram carries the target address the payload has to reach and the
 * fragment number zero: the module does not fragment, so every datagram it
 * builds is a whole one.
 *
 * @param[in] target Address and port the payload is sent to.
 * @param[in] payload Bytes of the payload, may be null while its size is zero.
 * @param[in] size Number of the bytes of the payload.
 * @return The bytes of the datagram, empty when the target cannot be sent.
 */
std::vector<std::uint8_t> BuildUdpDatagram(const Endpoint& target, const std::uint8_t* payload, std::size_t size);

/**
 * @brief Read the datagram of an association.
 *
 * A datagram which is fragmented (`FRAG` is not zero) is refused: the module
 * never builds one and does not reassemble what it receives, so a fragment
 * cannot be delivered to the application as a whole payload.
 *
 * @param[in] data Bytes of the datagram.
 * @param[in] size Number of the bytes which are available.
 * @param[out] sender Address and port which sent the payload, untouched on
 *                    failure.
 * @param[out] payload Bytes of the payload inside the datagram, untouched on
 *                     failure.
 * @param[out] payload_size Number of the bytes of the payload, untouched on
 *                          failure.
 * @return true when the datagram is complete and carries a whole payload.
 */
bool ParseUdpDatagram(const std::uint8_t* data, std::size_t size, Endpoint& sender, const std::uint8_t*& payload,
                      std::size_t& payload_size);

} // namespace socks5
} // namespace network
} // namespace appbox

#endif // APPBOX_SANDBOX_NETWORK_SOCKS5_HPP
