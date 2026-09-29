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
 * of the archive (see the schema below), the loader hands the path of the file
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
inline bool IsSocks5Token(std::string_view token)
{
    std::string normalized;
    normalized.reserve(token.size());

    for (const char character : token)
    {
        char lower = character;
        if (lower >= 'A' && lower <= 'Z')
        {
            lower = static_cast<char>(lower - 'A' + 'a');
        }
        if (lower == ' ' || lower == '-')
        {
            lower = '_';
        }
        normalized.push_back(lower);
    }

    return normalized == kSocks5Token;
}

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
inline PortText ReadPortText(std::string_view text, std::uint16_t& port)
{
    if (text.empty())
    {
        return PortText::Empty;
    }

    for (const char character : text)
    {
        if (character < '0' || character > '9')
        {
            return PortText::NotDecimal;
        }
    }

    if (text.size() > 1 && text.front() == '0')
    {
        return PortText::LeadingZero;
    }
    if (text.size() > 5)
    {
        /* The largest port has five digits, so a longer text is out of range. */
        return PortText::OutOfRange;
    }

    unsigned int value = 0;
    for (const char character : text)
    {
        value = value * 10 + static_cast<unsigned int>(character - '0');
    }
    if (value < 1 || value > 65535)
    {
        return PortText::OutOfRange;
    }

    port = static_cast<std::uint16_t>(value);
    return PortText::Ok;
}

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
inline bool IsHexDigit(char character)
{
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

/**
 * @brief Convert a hexadecimal digit to its value.
 * @param[in] character The character to convert.
 * @return The value of the digit, zero when the character is not a digit.
 */
inline std::uint8_t HexDigitValue(char character)
{
    if (character >= '0' && character <= '9')
    {
        return static_cast<std::uint8_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f')
    {
        return static_cast<std::uint8_t>(character - 'a' + 10);
    }
    if (character >= 'A' && character <= 'F')
    {
        return static_cast<std::uint8_t>(character - 'A' + 10);
    }
    return 0;
}

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
inline bool ParseIpv4(std::string_view text, Address& address)
{
    std::array<std::uint8_t, 16> bytes{};
    std::size_t                  octet = 0;
    std::size_t                  index = 0;

    while (index < text.size())
    {
        const std::size_t begin = index;
        while (index < text.size() && text[index] != '.')
        {
            if (text[index] < '0' || text[index] > '9')
            {
                return false;
            }
            ++index;
        }

        const std::size_t digits = index - begin;
        if (digits == 0 || digits > 3)
        {
            return false;
        }
        if (digits > 1 && text[begin] == '0')
        {
            /* A leading zero would be read as an octal number by some parsers. */
            return false;
        }

        unsigned int value = 0;
        for (std::size_t digit = begin; digit < index; ++digit)
        {
            value = value * 10 + static_cast<unsigned int>(text[digit] - '0');
        }
        if (value > 255 || octet >= 4)
        {
            return false;
        }
        bytes[octet] = static_cast<std::uint8_t>(value);
        ++octet;

        if (index < text.size())
        {
            /* Skip the separator. */
            ++index;
            if (index == text.size())
            {
                return false;
            }
        }
    }

    if (octet != 4)
    {
        return false;
    }

    address.family = AddressFamily::IPv4;
    address.bytes = bytes;
    return true;
}

/**
 * @brief Parse one group of an IPv6 address.
 *
 * @param[in] text The text of the group.
 * @param[out] value The parsed value.
 * @return true when the text holds one to four hexadecimal digits.
 */
inline bool ParseIpv6Group(std::string_view text, std::uint16_t& value)
{
    if (text.empty() || text.size() > 4)
    {
        return false;
    }

    std::uint16_t parsed = 0;
    for (const char character : text)
    {
        if (!IsHexDigit(character))
        {
            return false;
        }
        parsed = static_cast<std::uint16_t>((parsed << 4) | HexDigitValue(character));
    }

    value = parsed;
    return true;
}

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
inline bool ParseIpv6(std::string_view text, Address& address)
{
    if (text.empty() || text.find('%') != std::string_view::npos)
    {
        return false;
    }

    std::array<std::uint16_t, 8> groups{};
    std::size_t                  count = 0;
    std::size_t                  compressed = static_cast<std::size_t>(-1);
    std::size_t                  index = 0;

    if (text[0] == ':')
    {
        if (text.size() < 2 || text[1] != ':')
        {
            return false;
        }
        compressed = 0;
        index = 2;
    }

    while (index < text.size())
    {
        const std::size_t begin = index;
        while (index < text.size() && text[index] != ':')
        {
            ++index;
        }
        const std::string_view group = text.substr(begin, index - begin);

        if (group.find('.') != std::string_view::npos)
        {
            /* The last group can carry an embedded IPv4 address. */
            Address embedded;
            if (!ParseIpv4(group, embedded) || index != text.size() || count + 2 > 8)
            {
                return false;
            }
            groups[count] = static_cast<std::uint16_t>((embedded.bytes[0] << 8) | embedded.bytes[1]);
            groups[count + 1] = static_cast<std::uint16_t>((embedded.bytes[2] << 8) | embedded.bytes[3]);
            count += 2;
        }
        else
        {
            if (count >= 8 || !ParseIpv6Group(group, groups[count]))
            {
                return false;
            }
            ++count;
        }

        if (index == text.size())
        {
            break;
        }

        /* Skip the separator; a second one starts the compressed part. */
        ++index;
        if (index < text.size() && text[index] == ':')
        {
            if (compressed != static_cast<std::size_t>(-1))
            {
                return false;
            }
            compressed = count;
            ++index;
            if (index == text.size())
            {
                break;
            }
        }
        else if (index == text.size())
        {
            /* A single trailing colon is not an address. */
            return false;
        }
    }

    if (compressed == static_cast<std::size_t>(-1))
    {
        if (count != 8)
        {
            return false;
        }
    }
    else
    {
        if (count >= 8)
        {
            /* The compression has to elide at least one group. */
            return false;
        }

        const std::size_t elided = 8 - count;
        for (std::size_t position = count; position > compressed; --position)
        {
            groups[position + elided - 1] = groups[position - 1];
        }
        for (std::size_t position = compressed; position < compressed + elided; ++position)
        {
            groups[position] = 0;
        }
    }

    Address parsed;
    parsed.family = AddressFamily::IPv6;
    for (std::size_t position = 0; position < 8; ++position)
    {
        parsed.bytes[position * 2] = static_cast<std::uint8_t>(groups[position] >> 8);
        parsed.bytes[position * 2 + 1] = static_cast<std::uint8_t>(groups[position] & 0xff);
    }

    address = parsed;
    return true;
}

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
inline bool ParseAddress(std::string_view text, Address& address)
{
    if (text.find(':') != std::string_view::npos)
    {
        return ParseIpv6(text, address);
    }
    return ParseIpv4(text, address);
}

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
inline std::string NormalizeHostname(std::string_view hostname)
{
    std::string normalized;
    normalized.reserve(hostname.size());

    for (const char character : hostname)
    {
        char lower = character;
        if (lower >= 'A' && lower <= 'Z')
        {
            lower = static_cast<char>(lower - 'A' + 'a');
        }
        normalized.push_back(lower);
    }

    if (normalized.size() > 1 && normalized.back() == '.')
    {
        normalized.pop_back();
    }
    return normalized;
}

/**
 * @brief Whether two hostnames name the same host.
 * @param[in] left Left hostname.
 * @param[in] right Right hostname.
 * @return true when both names are equal after normalization.
 */
inline bool HostnamesEqual(std::string_view left, std::string_view right)
{
    return NormalizeHostname(left) == NormalizeHostname(right);
}

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
inline void to_json(nlohmann::json& json, const Entry& entry)
{
    json = nlohmann::json::object();
    json[kHostnameKey] = entry.hostname;
    json[kRedirectKey] = entry.redirect;
}

/**
 * @brief Store the proxy of the network isolation file.
 * @param[out] json Object which receives the proxy.
 * @param[in] proxy The proxy to store.
 */
inline void to_json(nlohmann::json& json, const Proxy& proxy)
{
    json = nlohmann::json::object();
    json[kProxyTypeKey] = proxy.type;
    json[kProxyTcpKey] = proxy.tcp;
    json[kProxyUdpKey] = proxy.udp;
    json[kProxyServerKey] = proxy.server;
    json[kProxyPortKey] = proxy.port;
    json[kProxyUsernameKey] = proxy.username;
    json[kProxyPasswordKey] = proxy.password;
}

/**
 * @brief Read one DNS redirection of the network isolation file.
 * @param[in] json Object holding the entry.
 * @param[out] entry The entry to fill.
 * @throw appbox::IsolationDocumentError The entry does not fit the schema.
 */
inline void from_json(const nlohmann::json& json, Entry& entry)
{
    const std::string holder = "a network isolation file entry";
    isolation_document::RequireObject(json, holder);

    entry.hostname = isolation_document::RequiredText(json, kHostnameKey, holder);
    entry.redirect = isolation_document::RequiredText(json, kRedirectKey, holder);

    if (entry.hostname.empty())
    {
        isolation_document::Throw("a network isolation file entry has an empty hostname");
    }
}

/**
 * @brief Read the proxy of the network isolation file.
 *
 * The members are read leniently, see Proxy.
 *
 * @param[in] json Object holding the proxy.
 * @param[out] proxy The proxy to fill.
 */
inline void from_json(const nlohmann::json& json, Proxy& proxy)
{
    proxy = Proxy{};
    if (!json.is_object())
    {
        return;
    }

    proxy.type = isolation_document::LenientText(json, kProxyTypeKey);
    proxy.tcp = isolation_document::LenientFlag(json, kProxyTcpKey);
    proxy.udp = isolation_document::LenientFlag(json, kProxyUdpKey);
    proxy.server = isolation_document::LenientText(json, kProxyServerKey);
    proxy.port = isolation_document::LenientText(json, kProxyPortKey);
    proxy.username = isolation_document::LenientText(json, kProxyUsernameKey);
    proxy.password = isolation_document::LenientText(json, kProxyPasswordKey);
}

/**
 * @brief Store the content of the network isolation file.
 * @param[out] json Object which receives the document.
 * @param[in] document The document to store.
 */
inline void to_json(nlohmann::json& json, const Document& document)
{
    json = nlohmann::json::object();
    json[kVersionKey] = document.version;

    nlohmann::json entries = nlohmann::json::array();
    for (const auto& entry : document.entries)
    {
        entries.push_back(nlohmann::json(entry));
    }
    json[kEntriesKey] = std::move(entries);

    if (document.proxy.has_value())
    {
        json[kProxyKey] = nlohmann::json(*document.proxy);
    }
}

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
inline void from_json(const nlohmann::json& json, Document& document)
{
    const std::string holder = "the network isolation file";
    isolation_document::RequireObject(json, holder);

    document = Document{};
    document.version = isolation_document::RequiredInt(json, kVersionKey, holder);

    if (const auto* entries = isolation_document::OptionalArray(json, kEntriesKey, holder))
    {
        for (const auto& item : *entries)
        {
            document.entries.push_back(item.get<Entry>());
        }
    }

    const auto* proxy = isolation_document::FindMember(json, kProxyKey);
    if (proxy != nullptr && proxy->is_object())
    {
        document.proxy = proxy->get<Proxy>();
    }
}

} // namespace network_isolation

} // namespace appbox

#endif // APPBOX_COMMON_NETWORK_ISOLATION_HPP
