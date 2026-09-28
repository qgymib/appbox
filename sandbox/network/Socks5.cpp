#include "Socks5.hpp"
#include "NetworkIsolation.hpp"
#include <array>

namespace
{

/**
 * @brief Render one hexadecimal digit in lower case.
 * @param[in] value Value of the digit, which has to be smaller than sixteen.
 * @return The character of the digit.
 */
char HexDigit(unsigned int value)
{
    return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + (value - 10u));
}

/**
 * @brief Render an IPv6 address in its compressed form.
 *
 * The longest run of zero groups which holds at least two groups is elided,
 * which is the form RFC 5952 asks for and the form the name resolution of the
 * operating system reads back as the same address.
 *
 * @param[in] bytes The sixteen bytes of the address.
 * @return The text of the address.
 */
std::string FormatIpv6(const std::uint8_t* bytes)
{
    std::array<std::uint16_t, 8> groups{};
    for (std::size_t index = 0; index < groups.size(); ++index)
    {
        groups[index] = static_cast<std::uint16_t>((bytes[index * 2] << 8) | bytes[index * 2 + 1]);
    }

    /* Find the longest run of zero groups; a run of one group is not elided. */
    std::size_t run_start = 0;
    std::size_t run_length = 0;
    std::size_t elided_start = groups.size();
    std::size_t elided_length = 0;
    for (std::size_t index = 0; index <= groups.size(); ++index)
    {
        if (index < groups.size() && groups[index] == 0)
        {
            if (run_length == 0)
            {
                run_start = index;
            }
            ++run_length;
            continue;
        }

        if (run_length > elided_length)
        {
            elided_start = run_start;
            elided_length = run_length;
        }
        run_length = 0;
    }

    if (elided_length < 2)
    {
        elided_start = groups.size();
        elided_length = 0;
    }

    std::string text;
    for (std::size_t index = 0; index < groups.size(); ++index)
    {
        if (index == elided_start)
        {
            text += "::";
            index += elided_length - 1;
            continue;
        }

        if (!text.empty() && text.back() != ':')
        {
            text += ':';
        }

        std::uint16_t value = groups[index];
        if (value == 0)
        {
            text += '0';
            continue;
        }

        char        digits[4];
        std::size_t count = 0;
        while (value != 0)
        {
            digits[count] = HexDigit(static_cast<unsigned int>(value & 0x0fu));
            value = static_cast<std::uint16_t>(value >> 4);
            ++count;
        }
        while (count > 0)
        {
            --count;
            text += digits[count];
        }
    }
    return text;
}

/**
 * @brief Append the address of an endpoint to a message.
 *
 * The type of the address is derived from the text of the host: a literal is
 * sent as its bytes and every other text as a domain name, which the server
 * resolves itself.
 *
 * @param[in,out] message Message to append the address to.
 * @param[in] target Endpoint which carries the address.
 * @return true when the address fits a message.
 */
bool AppendEndpoint(std::vector<std::uint8_t>& message, const appbox::network::socks5::Endpoint& target)
{
    appbox::network_isolation::Address address;
    if (appbox::network_isolation::ParseAddress(target.host, address))
    {
        if (address.family == appbox::network_isolation::AddressFamily::IPv6)
        {
            message.push_back(static_cast<std::uint8_t>(appbox::network::socks5::AddressType::IPv6));
            message.insert(message.end(), address.bytes.begin(), address.bytes.begin() + 16);
        }
        else
        {
            message.push_back(static_cast<std::uint8_t>(appbox::network::socks5::AddressType::IPv4));
            message.insert(message.end(), address.bytes.begin(), address.bytes.begin() + 4);
        }
    }
    else
    {
        if (target.host.empty() || target.host.size() > appbox::network::socks5::kMaxDomainNameLength)
        {
            return false;
        }

        message.push_back(static_cast<std::uint8_t>(appbox::network::socks5::AddressType::DomainName));
        message.push_back(static_cast<std::uint8_t>(target.host.size()));
        message.insert(message.end(), target.host.begin(), target.host.end());
    }

    message.push_back(static_cast<std::uint8_t>(target.port >> 8));
    message.push_back(static_cast<std::uint8_t>(target.port & 0xff));
    return true;
}

/**
 * @brief Read the address of a message.
 *
 * @param[in] data Bytes of the message.
 * @param[in] size Number of the bytes which are available.
 * @param[in] offset Position of the type of the address.
 * @param[out] out The endpoint which was read, untouched on failure.
 * @param[out] next Position of the first byte after the address, untouched on
 *                  failure.
 * @return true when the whole address is available.
 */
bool ReadEndpoint(const std::uint8_t* data, std::size_t size, std::size_t offset,
                  appbox::network::socks5::Endpoint& out, std::size_t& next)
{
    if (offset >= size)
    {
        return false;
    }

    using appbox::network::socks5::AddressType;
    const auto type = static_cast<AddressType>(data[offset]);
    ++offset;

    appbox::network::socks5::Endpoint endpoint;
    endpoint.type = type;

    switch (type)
    {
    case AddressType::IPv4:
        if (size - offset < 4)
        {
            return false;
        }
        endpoint.host = appbox::network::socks5::FormatAddress(type, data + offset, 4);
        offset += 4;
        break;

    case AddressType::IPv6:
        if (size - offset < 16)
        {
            return false;
        }
        endpoint.host = appbox::network::socks5::FormatAddress(type, data + offset, 16);
        offset += 16;
        break;

    case AddressType::DomainName: {
        if (offset >= size)
        {
            return false;
        }

        const std::size_t length = data[offset];
        ++offset;
        if (length == 0 || size - offset < length)
        {
            return false;
        }

        endpoint.host.assign(reinterpret_cast<const char*>(data + offset), length);
        offset += length;
        break;
    }

    default:
        return false;
    }

    if (size - offset < 2)
    {
        return false;
    }
    endpoint.port = static_cast<std::uint16_t>((data[offset] << 8) | data[offset + 1]);
    offset += 2;

    out = endpoint;
    next = offset;
    return true;
}

} // namespace

bool appbox::network::socks5::EndpointsEqual(const Endpoint& left, const Endpoint& right)
{
    return left.port == right.port && left.host == right.host;
}

std::string appbox::network::socks5::FormatAddress(AddressType type, const std::uint8_t* bytes, std::size_t size)
{
    if (bytes == nullptr)
    {
        return {};
    }

    if (type == AddressType::IPv4)
    {
        if (size < 4)
        {
            return {};
        }

        std::string text;
        for (std::size_t index = 0; index < 4; ++index)
        {
            if (index != 0)
            {
                text += '.';
            }
            text += std::to_string(static_cast<unsigned int>(bytes[index]));
        }
        return text;
    }

    if (type == AddressType::IPv6)
    {
        if (size < 16)
        {
            return {};
        }
        return FormatIpv6(bytes);
    }

    /*
     * A domain name is carried as text already, so there is nothing to render
     * for it.
     */
    return {};
}

std::vector<std::uint8_t> appbox::network::socks5::BuildGreeting(bool withCredentials)
{
    std::vector<std::uint8_t> message;
    message.push_back(kVersion);

    if (withCredentials)
    {
        message.push_back(static_cast<std::uint8_t>(2));
        message.push_back(static_cast<std::uint8_t>(Method::NoAuthentication));
        message.push_back(static_cast<std::uint8_t>(Method::UserPassword));
        return message;
    }

    message.push_back(static_cast<std::uint8_t>(1));
    message.push_back(static_cast<std::uint8_t>(Method::NoAuthentication));
    return message;
}

bool appbox::network::socks5::ParseMethodSelection(const std::uint8_t* data, std::size_t size, Method& method)
{
    if (data == nullptr || size < 2 || data[0] != kVersion)
    {
        return false;
    }

    method = static_cast<Method>(data[1]);
    return true;
}

std::vector<std::uint8_t> appbox::network::socks5::BuildUserPasswordRequest(std::string_view user,
                                                                            std::string_view password)
{
    if (user.size() > kMaxCredentialLength || password.size() > kMaxCredentialLength)
    {
        return {};
    }

    std::vector<std::uint8_t> message;
    message.push_back(kAuthVersion);
    message.push_back(static_cast<std::uint8_t>(user.size()));
    message.insert(message.end(), user.begin(), user.end());
    message.push_back(static_cast<std::uint8_t>(password.size()));
    message.insert(message.end(), password.begin(), password.end());
    return message;
}

bool appbox::network::socks5::ParseUserPasswordReply(const std::uint8_t* data, std::size_t size)
{
    if (data == nullptr || size < 2 || data[0] != kAuthVersion)
    {
        return false;
    }
    return data[1] == 0x00;
}

std::vector<std::uint8_t> appbox::network::socks5::BuildRequest(Command command, const Endpoint& target)
{
    std::vector<std::uint8_t> message;
    message.push_back(kVersion);
    message.push_back(static_cast<std::uint8_t>(command));
    message.push_back(static_cast<std::uint8_t>(0x00));
    if (!AppendEndpoint(message, target))
    {
        return {};
    }
    return message;
}

bool appbox::network::socks5::ParseReply(const std::uint8_t* data, std::size_t size, Reply& reply, Endpoint& bound)
{
    if (data == nullptr || size < kHeaderSize || data[0] != kVersion)
    {
        return false;
    }

    Endpoint    endpoint;
    std::size_t next = 0;
    if (!ReadEndpoint(data, size, 3, endpoint, next))
    {
        return false;
    }

    reply = static_cast<Reply>(data[1]);
    bound = endpoint;
    return true;
}

std::size_t appbox::network::socks5::AddressSize(AddressType type)
{
    if (type == AddressType::IPv4)
    {
        return 4;
    }
    if (type == AddressType::IPv6)
    {
        return 16;
    }

    /* A domain name is counted, so its length is not a property of its type. */
    return 0;
}

std::vector<std::uint8_t> appbox::network::socks5::BuildUdpDatagram(const Endpoint& target, const std::uint8_t* payload,
                                                                    std::size_t size)
{
    std::vector<std::uint8_t> datagram;
    datagram.push_back(static_cast<std::uint8_t>(0x00)); /* Reserved. */
    datagram.push_back(static_cast<std::uint8_t>(0x00)); /* Reserved. */
    datagram.push_back(static_cast<std::uint8_t>(0x00)); /* Fragment number: the module never fragments. */
    if (!AppendEndpoint(datagram, target))
    {
        return {};
    }

    if (payload != nullptr && size != 0)
    {
        datagram.insert(datagram.end(), payload, payload + size);
    }
    return datagram;
}

bool appbox::network::socks5::ParseUdpDatagram(const std::uint8_t* data, std::size_t size, Endpoint& sender,
                                               const std::uint8_t*& payload, std::size_t& payload_size)
{
    if (data == nullptr || size < kUdpHeaderSize)
    {
        return false;
    }
    if (data[0] != 0x00 || data[1] != 0x00)
    {
        return false;
    }
    if (data[2] != 0x00)
    {
        /* A fragmented datagram cannot be delivered as a whole payload. */
        return false;
    }

    Endpoint    endpoint;
    std::size_t next = 0;
    if (!ReadEndpoint(data, size, 3, endpoint, next))
    {
        return false;
    }

    sender = endpoint;
    payload = data + next;
    payload_size = size - next;
    return true;
}
