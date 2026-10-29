#ifndef APPBOX_COMMON_NETWORK_ISOLATION_HPP
#define APPBOX_COMMON_NETWORK_ISOLATION_HPP

#include "IsolationDocument.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace appbox
{

/**
 * @brief Isolation of the network traffic of a sandboxed application.
 *
 * The packer collects the network configuration of the packaged application in
 * the `Network` workspace: the DNS redirections of its `DNS` page pair a
 * hostname with the address the name has to resolve to inside the sandbox, and
 * its `Proxy` page holds the SOCKS5 proxy which carries the TCP traffic, the
 * UDP traffic or both. The packer writes both into the network isolation file
 * of the archive (see the schema below), the launcher hands the path of the file
 * to the sandbox, and the sandbox answers a matching name resolution with the
 * configured address instead of asking the host and sends the traffic of the
 * application through the configured proxy.
 *
 * The vocabulary lives in `common/` because the packer validates the
 * configuration with it and the sandbox applies it with it, so both sides
 * agree on what a valid redirect address, the same hostname and a valid port
 * are.
 */
namespace network_isolation
{

/**
 * @brief Schema of the network isolation file.
 *
 * The file is the JSON document which carries the network configuration from
 * the packer to the sandbox. The packer writes it into the overlay of the
 * archive, the sandbox answers a name resolution from its entries and sends
 * the traffic of the application through its proxy.
 *
 * ```
 * {
 *   "version": 1,
 *   "entries": [ { "hostname": "update.example.com", "redirect": "127.0.0.1" } ],
 *   "proxy": { "type": "socks5", "tcp": true, "udp": false,
 *              "server": "127.0.0.1", "port": "1080",
 *              "username": "user", "password": "secret" }
 * }
 * ```
 *
 * The hostname of an entry is a hostname or an IP address, which is the value
 * the `Hostname or IP Address` column of the workspace shows; the redirect is
 * the address the name resolves to inside the sandbox, which has to be an IPv4
 * or an IPv6 address literal.
 *
 * The `proxy` member is optional: it is written while the workspace holds a
 * proxy configuration and a file which does not carry it describes a session
 * without a proxy. The schema version therefore stays `1`, so a file which was
 * written before the member existed is still accepted.
 */

/**
 * @brief Version of the network isolation file written by the packer.
 *
 * A file of a different version is rejected instead of being interpreted with
 * the rules of another schema.
 */
inline constexpr int kVersion = 1;

/** Member name of the schema version. */
inline constexpr const char* kVersionKey = "version";

/** Member name of the entry list. */
inline constexpr const char* kEntriesKey = "entries";

/** Member name of the redirected hostname of an entry. */
inline constexpr const char* kHostnameKey = "hostname";

/** Member name of the redirect address of an entry. */
inline constexpr const char* kRedirectKey = "redirect";

/** Member name of the optional proxy configuration. */
inline constexpr const char* kProxyKey = "proxy";

/** Member name of the protocol of the proxy. */
inline constexpr const char* kProxyTypeKey = "type";

/** Member name of the flag which proxies the TCP traffic. */
inline constexpr const char* kProxyTcpKey = "tcp";

/** Member name of the flag which proxies the UDP traffic. */
inline constexpr const char* kProxyUdpKey = "udp";

/** Member name of the server of the proxy. */
inline constexpr const char* kProxyServerKey = "server";

/** Member name of the port of the proxy. */
inline constexpr const char* kProxyPortKey = "port";

/** Member name of the optional user name of the proxy. */
inline constexpr const char* kProxyUsernameKey = "username";

/** Member name of the optional password of the proxy. */
inline constexpr const char* kProxyPasswordKey = "password";

/**
 * @brief Token of the SOCKS5 protocol.
 *
 * The token is the one the `type` member of the proxy configuration carries
 * and the one the project file stores, so both documents name the protocol the
 * same way.
 */
inline constexpr const char* kSocks5Token = "socks5";

/**
 * @brief Whether a token names the SOCKS5 protocol.
 *
 * The comparison ignores the case and accepts a space or a dash in place of
 * the underscore of the canonical token, like the tokens of the isolation
 * modes do. The tokens are ASCII, so they are handled as narrow text like the
 * JSON documents which carry them.
 *
 * @param[in] token The token to read.
 * @return true when the token names SOCKS5.
 */
inline bool IsSocks5Token(std::string_view token);

/**
 * @brief Outcome of reading the text of a proxy port.
 *
 * The port is stored as the text the user entered, so both the packer and the
 * sandbox have to agree on what a valid port is. The outcome names the rule
 * which failed instead of a single flag, so the packer can report the value
 * with its own wording while the sandbox only asks whether the text is a port.
 */
enum class PortText
{
    Ok,          ///< The text is a port.
    Empty,       ///< The text is empty, which is the port which was not entered.
    NotDecimal,  ///< The text holds a character which is not a decimal digit.
    LeadingZero, ///< The text carries more than one digit and starts with a zero.
    OutOfRange   ///< The text is not between 1 and 65535.
};

/**
 * @brief Read the text of a proxy port.
 *
 * A port is a decimal number between 1 and 65535 without a leading zero, so
 * the text the user entered is never silently read as another port.
 *
 * @param[in] text The text to read.
 * @param[out] port The port of the text, untouched unless the text is a port.
 * @return The outcome of the reading.
 */
inline PortText ReadPortText(std::string_view text, std::uint16_t& port);

/**
 * @brief Address family of a redirect address.
 */
enum class AddressFamily
{
    IPv4, ///< The address holds four bytes.
    IPv6  ///< The address holds sixteen bytes.
};

/**
 * @brief A parsed IPv4 or IPv6 address.
 */
struct Address
{
    /**
     * @brief Family of the address.
     */
    AddressFamily family = AddressFamily::IPv4;

    /**
     * @brief Raw bytes of the address, in network order.
     *
     * Only the first four bytes are meaningful for an IPv4 address.
     */
    std::array<std::uint8_t, 16> bytes{};
};

/**
 * @brief Whether a text is a hexadecimal digit.
 * @param[in] character The character to inspect.
 * @return true when the character is `0`-`9`, `a`-`f` or `A`-`F`.
 */
inline bool IsHexDigit(char character);

/**
 * @brief Convert a hexadecimal digit to its value.
 * @param[in] character The character to convert.
 * @return The value of the digit, zero when the character is not a digit.
 */
inline std::uint8_t HexDigitValue(char character);

/**
 * @brief Parse a dotted quad IPv4 address.
 *
 * The text has to hold exactly four decimal octets of one to three digits; a
 * leading zero is rejected, exactly like the address parser of the operating
 * system rejects it, so a text which this function accepts is also accepted by
 * the name resolution which the sandbox redirects.
 *
 * @param[in] text The text to parse.
 * @param[out] address The parsed address, untouched on failure.
 * @return true when the text is a dotted quad IPv4 address.
 */
inline bool ParseIpv4(std::string_view text, Address& address);

/**
 * @brief Parse one group of an IPv6 address.
 *
 * @param[in] text The text of the group.
 * @param[out] value The parsed value.
 * @return true when the text holds one to four hexadecimal digits.
 */
inline bool ParseIpv6Group(std::string_view text, std::uint16_t& value);

/**
 * @brief Parse an IPv6 address, including a compressed and an embedded form.
 *
 * The accepted syntax is the one of the operating system: at most one `::`
 * which elides at least one group, one to four hexadecimal digits per group,
 * and an embedded dotted quad as the last group. A scope identifier (`%`) is
 * rejected, because it names an interface of the host instead of an address.
 *
 * @param[in] text The text to parse.
 * @param[out] address The parsed address, untouched on failure.
 * @return true when the text is an IPv6 address.
 */
inline bool ParseIpv6(std::string_view text, Address& address);

/**
 * @brief Parse an IPv4 or an IPv6 address literal.
 *
 * The text has to be an address literal of the form the name resolution of the
 * operating system accepts as a numeric host, because the sandbox answers a
 * redirected name with the literal the packer stored.
 *
 * @param[in] text The text to parse.
 * @param[out] address The parsed address, untouched on failure.
 * @return true when the text is an address literal.
 */
inline bool ParseAddress(std::string_view text, Address& address);

/**
 * @brief Normalize a hostname for the comparison.
 *
 * The name resolution of the host ignores the case of a name, and a trailing
 * dot names the very same host (it is the root label). Both are removed, so
 * `Update.Example.COM.` and `update.example.com` compare as the same host.
 *
 * @param[in] hostname The hostname to normalize.
 * @return The normalized hostname.
 */
inline std::string NormalizeHostname(std::string_view hostname);

/**
 * @brief Whether two hostnames name the same host.
 * @param[in] left Left hostname.
 * @param[in] right Right hostname.
 * @return true when both names are equal after normalization.
 */
inline bool HostnamesEqual(std::string_view left, std::string_view right);

/**
 * @brief One DNS redirection of the network isolation file.
 *
 * The structure is the schema of one listed hostname: the packer fills it while
 * it writes the file of the workspace, and the sandbox reads the file back into
 * the same structure, so neither side parses the JSON object of an entry member
 * by member.
 */
struct Entry
{
    /**
     * @brief Hostname or IP address the entry redirects, in UTF-8.
     */
    std::string hostname;

    /**
     * @brief Address the hostname resolves to inside the sandbox, in UTF-8.
     *
     * The text is the IPv4 or IPv6 address literal the packer stored. The
     * reader of the file validates it with ParseAddress() while it applies the
     * entry.
     */
    std::string redirect;
};

/**
 * @brief The proxy of the network isolation file.
 *
 * The structure is the schema of the optional `proxy` member. Its members are
 * read leniently: a proxy which cannot be used describes a session without a
 * proxy and never fails the whole document, so a missing member and a member of
 * another type leave the default of the structure instead of an error. The
 * rules which decide whether the proxy can be used at all (the protocol, the
 * server and the port) are applied by the sandbox while it applies the file,
 * which is also what the `Network` workspace of the packer does.
 */
struct Proxy
{
    /**
     * @brief Protocol of the proxy, which has to name SOCKS5.
     */
    std::string type;

    /**
     * @brief Whether the TCP traffic is carried by the proxy.
     */
    bool tcp = false;

    /**
     * @brief Whether the UDP traffic is carried by the proxy.
     */
    bool udp = false;

    /**
     * @brief Hostname or address of the server, in UTF-8.
     */
    std::string server;

    /**
     * @brief Port of the server, as the text the user entered.
     */
    std::string port;

    /**
     * @brief Optional user name, empty while the server asks for none.
     */
    std::string username;

    /**
     * @brief Optional password, empty while the server asks for none.
     */
    std::string password;
};

/**
 * @brief The content of the network isolation file.
 */
struct Document
{
    /**
     * @brief Schema version the document was written with.
     *
     * The reader compares the version with kVersion and refuses a document of
     * another version, so a file of a newer schema is never read with the rules
     * of this one.
     */
    int version = kVersion;

    /**
     * @brief The listed DNS redirections, in the order of the file.
     */
    std::vector<Entry> entries;

    /**
     * @brief The proxy of the session, empty while the file holds none.
     */
    std::optional<Proxy> proxy;
};

/**
 * @brief Store one DNS redirection of the network isolation file.
 * @param[out] json Object which receives the entry.
 * @param[in] entry The entry to store.
 */
inline void to_json(nlohmann::json& json, const Entry& entry);

/**
 * @brief Store the proxy of the network isolation file.
 * @param[out] json Object which receives the proxy.
 * @param[in] proxy The proxy to store.
 */
inline void to_json(nlohmann::json& json, const Proxy& proxy);

/**
 * @brief Read one DNS redirection of the network isolation file.
 * @param[in] json Object holding the entry.
 * @param[out] entry The entry to fill.
 * @throw appbox::IsolationDocumentError The entry does not fit the schema.
 */
inline void from_json(const nlohmann::json& json, Entry& entry);

/**
 * @brief Read the proxy of the network isolation file.
 *
 * The members are read leniently, see Proxy.
 *
 * @param[in] json Object holding the proxy.
 * @param[out] proxy The proxy to fill.
 */
inline void from_json(const nlohmann::json& json, Proxy& proxy);

/**
 * @brief Store the content of the network isolation file.
 * @param[out] json Object which receives the document.
 * @param[in] document The document to store.
 */
inline void to_json(nlohmann::json& json, const Document& document);

/**
 * @brief Read the content of the network isolation file.
 *
 * A document which does not list a redirection and which carries no proxy
 * describes a session without a network isolation, which is what a session
 * without a configuration writes. The `proxy` member is only read while it is
 * an object: a member of another type describes a session without a proxy.
 *
 * @param[in] json Object holding the document.
 * @param[out] document The document to fill.
 * @throw appbox::IsolationDocumentError The document does not fit the schema.
 */
inline void from_json(const nlohmann::json& json, Document& document);

} // namespace network_isolation

} // namespace appbox

/*
 * The address parsing, the hostname normalization and the JSON members of the
 * schema are the implementation of the declarations above, so they live in a
 * detail header which only this header includes.
 */
#include "details/NetworkIsolationDetail.hpp"

#endif // APPBOX_COMMON_NETWORK_ISOLATION_HPP
