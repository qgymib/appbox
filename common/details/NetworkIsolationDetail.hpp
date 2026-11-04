#ifndef APPBOX_COMMON_DETAILS_NETWORK_ISOLATION_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_NETWORK_ISOLATION_DETAIL_HPP

/*
 * Implementation of `common/NetworkIsolation.hpp`: the reading of the proxy port
 * and the SOCKS5 token, the IPv4 and IPv6 parsing, the hostname normalization,
 * the JSON members of the schema and the noexcept reader of a document. The
 * header is included by the public header and is not meant to be included on its
 * own.
 */

#include "NetworkIsolation.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <utility>
#include <vector>

namespace appbox
{
namespace network_isolation
{

/* Definition of the contract on the declaration. */
inline bool IsSocks5Token(std::string_view token) noexcept
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

/* Definition of the contract on the declaration. */
inline tl::expected<std::uint16_t, ErrorCode> ReadPortText(std::string_view text) noexcept
{
    if (text.empty())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPortEmpty);
    }

    for (const char character : text)
    {
        if (character < '0' || character > '9')
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kPortNotDecimal);
        }
    }

    if (text.size() > 1 && text.front() == '0')
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPortLeadingZero);
    }
    if (text.size() > 5)
    {
        /* The largest port has five digits, so a longer text is out of range. */
        return tl::unexpected<ErrorCode>(ErrorCode::kPortOutOfRange);
    }

    unsigned int value = 0;
    for (const char character : text)
    {
        value = value * 10 + static_cast<unsigned int>(character - '0');
    }
    if (value < 1 || value > 65535)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPortOutOfRange);
    }

    return static_cast<std::uint16_t>(value);
}

/* Definition of the contract on the declaration. */
inline bool IsHexDigit(char character) noexcept
{
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

/* Definition of the contract on the declaration. */
inline std::uint8_t HexDigitValue(char character) noexcept
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

/* Definition of the contract on the declaration. */
inline tl::expected<Address, ErrorCode> ParseIpv4(std::string_view text) noexcept
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
                return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
            }
            ++index;
        }

        const std::size_t digits = index - begin;
        if (digits == 0 || digits > 3)
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
        }
        if (digits > 1 && text[begin] == '0')
        {
            /* A leading zero would be read as an octal number by some parsers. */
            return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
        }

        unsigned int value = 0;
        for (std::size_t digit = begin; digit < index; ++digit)
        {
            value = value * 10 + static_cast<unsigned int>(text[digit] - '0');
        }
        if (value > 255 || octet >= 4)
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
        }
        bytes[octet] = static_cast<std::uint8_t>(value);
        ++octet;

        if (index < text.size())
        {
            /* Skip the separator. */
            ++index;
            if (index == text.size())
            {
                return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
            }
        }
    }

    if (octet != 4)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
    }

    Address address;
    address.family = AddressFamily::IPv4;
    address.bytes = bytes;
    return address;
}

/* Definition of the contract on the declaration. */
inline tl::expected<std::uint16_t, ErrorCode> ParseIpv6Group(std::string_view text) noexcept
{
    if (text.empty() || text.size() > 4)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
    }

    std::uint16_t parsed = 0;
    for (const char character : text)
    {
        if (!IsHexDigit(character))
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
        }
        parsed = static_cast<std::uint16_t>((parsed << 4) | HexDigitValue(character));
    }

    return parsed;
}

/* Definition of the contract on the declaration. */
inline tl::expected<Address, ErrorCode> ParseIpv6(std::string_view text) noexcept
{
    if (text.empty() || text.find('%') != std::string_view::npos)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
    }

    std::array<std::uint16_t, 8> groups{};
    std::size_t                  count = 0;
    std::size_t                  compressed = static_cast<std::size_t>(-1);
    std::size_t                  index = 0;

    if (text[0] == ':')
    {
        if (text.size() < 2 || text[1] != ':')
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
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
            const auto embedded = ParseIpv4(group);
            if (!embedded || index != text.size() || count + 2 > 8)
            {
                return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
            }
            groups[count] = static_cast<std::uint16_t>((embedded->bytes[0] << 8) | embedded->bytes[1]);
            groups[count + 1] = static_cast<std::uint16_t>((embedded->bytes[2] << 8) | embedded->bytes[3]);
            count += 2;
        }
        else
        {
            if (count >= 8)
            {
                return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
            }

            const auto parsed = ParseIpv6Group(group);
            if (!parsed)
            {
                return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
            }
            groups[count] = *parsed;
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
                return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
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
            return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
        }
    }

    if (compressed == static_cast<std::size_t>(-1))
    {
        if (count != 8)
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
        }
    }
    else
    {
        if (count >= 8)
        {
            /* The compression has to elide at least one group. */
            return tl::unexpected<ErrorCode>(ErrorCode::kAddressMalformed);
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

    return parsed;
}

/* Definition of the contract on the declaration. */
inline tl::expected<Address, ErrorCode> ParseAddress(std::string_view text) noexcept
{
    if (text.find(':') != std::string_view::npos)
    {
        return ParseIpv6(text);
    }
    return ParseIpv4(text);
}

/* Definition of the contract on the declaration. */
inline std::string NormalizeHostname(std::string_view hostname) noexcept
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

/* Definition of the contract on the declaration. */
inline bool HostnamesEqual(std::string_view left, std::string_view right) noexcept
{
    return NormalizeHostname(left) == NormalizeHostname(right);
}

/* Definition of the contract on the declaration. */
inline void to_json(nlohmann::json& json, const Entry& entry)
{
    json = nlohmann::json::object();
    json[kHostnameKey] = entry.hostname;
    json[kRedirectKey] = entry.redirect;
}

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
inline tl::expected<Document, ErrorCode> ParseDocument(std::string_view text) noexcept
{
    /*
     * The JSON layer runs with the exception reporting switched off, so a text
     * which is no JSON document is answered by a discarded value, and the member
     * layer is the `from_json()` of the schema, which reports a refused member
     * through `IsolationDocumentError`. Both are turned into the code of the
     * table here, so the reader of a file never sees an exception and never
     * receives a text.
     */
    const auto parsed = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kIsolationDocumentInvalid);
    }

    const auto* version = isolation_document::FindMember(parsed, kVersionKey);
    if (version == nullptr || !version->is_number_integer())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kIsolationDocumentInvalid);
    }
    if (version->get<int>() != kVersion)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kIsolationDocumentUnsupportedVersion);
    }

    try
    {
        return parsed.get<Document>();
    }
    catch (const std::exception&)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kIsolationDocumentInvalid);
    }
}

} // namespace network_isolation
} // namespace appbox

#endif
