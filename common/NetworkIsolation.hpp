#ifndef APPBOX_COMMON_NETWORK_ISOLATION_HPP
#define APPBOX_COMMON_NETWORK_ISOLATION_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace appbox
{

/**
 * @brief Isolation of the name resolution of a sandboxed application.
 *
 * The packer collects the DNS redirections of the packaged application in the
 * `DNS` page of the `Network` workspace: every entry pairs a hostname with the
 * address the name has to resolve to inside the sandbox. The packer writes the
 * entries into the network isolation file of the archive (see the schema
 * below), the loader hands the path of the file to the sandbox, and the
 * sandbox answers a matching name resolution with the configured address
 * instead of asking the host: the resolution is served from the isolation file
 * and never leaves the process.
 *
 * The vocabulary lives in `common/` because the packer validates the entries
 * with it and the sandbox resolves them with it, so both sides agree on what a
 * valid redirect address and the same hostname are.
 */
namespace network_isolation
{

/**
 * @brief Schema of the network isolation file.
 *
 * The file is the JSON document which carries the DNS redirections from the
 * packer to the sandbox. The packer writes it into the overlay of the archive
 * and the sandbox answers a name resolution from it.
 *
 * ```
 * {
 *   "version": 1,
 *   "entries": [ { "hostname": "update.example.com", "redirect": "127.0.0.1" } ]
 * }
 * ```
 *
 * The hostname of an entry is a hostname or an IP address, which is the value
 * the `Hostname or IP Address` column of the workspace shows; the redirect is
 * the address the name resolves to inside the sandbox, which has to be an IPv4
 * or an IPv6 address literal.
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

} // namespace network_isolation

} // namespace appbox

#endif // APPBOX_COMMON_NETWORK_ISOLATION_HPP
