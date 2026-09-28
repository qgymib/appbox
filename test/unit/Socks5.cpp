#include <gtest/gtest.h>
#include "network/Socks5.hpp"
#include <cstdint>
#include <string>
#include <vector>

using appbox::network::socks5::AddressType;
using appbox::network::socks5::Command;
using appbox::network::socks5::Endpoint;
using appbox::network::socks5::Method;
using appbox::network::socks5::Reply;

namespace
{

/**
 * @brief Build an endpoint of an address literal.
 * @param[in] host Text of the address.
 * @param[in] port Port of the endpoint.
 * @return The endpoint.
 */
Endpoint Literal(const std::string& host, std::uint16_t port)
{
    Endpoint endpoint;
    endpoint.host = host;
    endpoint.port = port;
    return endpoint;
}

/**
 * @brief Read a reply and fail the test when it is incomplete.
 * @param[in] message Bytes of the reply.
 * @param[out] reply The reply code.
 * @param[out] bound The address of the reply.
 */
void ParseReplyOrFail(const std::vector<std::uint8_t>& message, Reply& reply, Endpoint& bound)
{
    ASSERT_TRUE(appbox::network::socks5::ParseReply(message.data(), message.size(), reply, bound));
}

} // namespace

TEST(Unit_Socks5, GreetingOffersNoAuthentication)
{
    const std::vector<std::uint8_t> expected{ 0x05, 0x01, 0x00 };
    EXPECT_EQ(appbox::network::socks5::BuildGreeting(false), expected);
}

TEST(Unit_Socks5, GreetingOffersTheCredentialsAsWell)
{
    const std::vector<std::uint8_t> expected{ 0x05, 0x02, 0x00, 0x02 };
    EXPECT_EQ(appbox::network::socks5::BuildGreeting(true), expected);
}

TEST(Unit_Socks5, ReadsTheSelectedMethod)
{
    const std::vector<std::uint8_t> message{ 0x05, 0x02 };
    Method                          method = Method::NoAcceptable;

    ASSERT_TRUE(appbox::network::socks5::ParseMethodSelection(message.data(), message.size(), method));
    EXPECT_EQ(method, Method::UserPassword);
}

TEST(Unit_Socks5, ReadsTheMethodWhichTheServerRefuses)
{
    const std::vector<std::uint8_t> message{ 0x05, 0xFF };
    Method                          method = Method::NoAuthentication;

    ASSERT_TRUE(appbox::network::socks5::ParseMethodSelection(message.data(), message.size(), method));
    EXPECT_EQ(method, Method::NoAcceptable);
}

TEST(Unit_Socks5, RefusesAnIncompleteOrForeignMethodSelection)
{
    const std::vector<std::uint8_t> incomplete{ 0x05 };
    const std::vector<std::uint8_t> foreign{ 0x04, 0x00 };
    Method                          method = Method::NoAuthentication;

    EXPECT_FALSE(appbox::network::socks5::ParseMethodSelection(incomplete.data(), incomplete.size(), method));
    EXPECT_FALSE(appbox::network::socks5::ParseMethodSelection(foreign.data(), foreign.size(), method));
    EXPECT_FALSE(appbox::network::socks5::ParseMethodSelection(nullptr, 0, method));
}

TEST(Unit_Socks5, BuildsTheUserPasswordRequest)
{
    const std::vector<std::uint8_t> expected{ 0x01, 0x04, 'u', 's', 'e', 'r', 0x03, 'p', 'w', 'd' };
    EXPECT_EQ(appbox::network::socks5::BuildUserPasswordRequest("user", "pwd"), expected);
}

TEST(Unit_Socks5, RefusesACredentialWhichDoesNotFitTheMessage)
{
    const std::string too_long(appbox::network::socks5::kMaxCredentialLength + 1, 'x');

    EXPECT_TRUE(appbox::network::socks5::BuildUserPasswordRequest(too_long, "pwd").empty());
    EXPECT_TRUE(appbox::network::socks5::BuildUserPasswordRequest("user", too_long).empty());
}

TEST(Unit_Socks5, ReadsTheUserPasswordReply)
{
    const std::vector<std::uint8_t> accepted{ 0x01, 0x00 };
    const std::vector<std::uint8_t> refused{ 0x01, 0x01 };
    const std::vector<std::uint8_t> incomplete{ 0x01 };

    EXPECT_TRUE(appbox::network::socks5::ParseUserPasswordReply(accepted.data(), accepted.size()));
    EXPECT_FALSE(appbox::network::socks5::ParseUserPasswordReply(refused.data(), refused.size()));
    EXPECT_FALSE(appbox::network::socks5::ParseUserPasswordReply(incomplete.data(), incomplete.size()));
    EXPECT_FALSE(appbox::network::socks5::ParseUserPasswordReply(nullptr, 0));
}

TEST(Unit_Socks5, BuildsARequestForAnIpv4Address)
{
    const std::vector<std::uint8_t> expected{ 0x05, 0x01, 0x00, 0x01, 0x7F, 0x00, 0x00, 0x01, 0x04, 0x38 };
    EXPECT_EQ(appbox::network::socks5::BuildRequest(Command::Connect, Literal("127.0.0.1", 1080)), expected);
}

TEST(Unit_Socks5, BuildsARequestForAnIpv6Address)
{
    std::vector<std::uint8_t> expected{ 0x05, 0x03, 0x00, 0x04 };
    expected.insert(expected.end(), 15, 0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x35);

    EXPECT_EQ(appbox::network::socks5::BuildRequest(Command::UdpAssociate, Literal("::1", 53)), expected);
}

TEST(Unit_Socks5, BuildsARequestForADomainName)
{
    const std::vector<std::uint8_t> expected{ 0x05, 0x01, 0x00, 0x03, 0x0B, 'e', 'x', 'a',  'm',
                                              'p',  'l',  'e',  '.',  'c',  'o', 'm', 0x00, 0x50 };

    EXPECT_EQ(appbox::network::socks5::BuildRequest(Command::Connect, Literal("example.com", 80)), expected);
}

TEST(Unit_Socks5, RefusesADomainNameWhichDoesNotFitTheMessage)
{
    const std::string too_long(appbox::network::socks5::kMaxDomainNameLength + 1, 'x');

    EXPECT_TRUE(appbox::network::socks5::BuildRequest(Command::Connect, Literal(too_long, 80)).empty());
    EXPECT_TRUE(appbox::network::socks5::BuildRequest(Command::Connect, Literal("", 80)).empty());
}

TEST(Unit_Socks5, ReadsTheAddressOfAReply)
{
    const std::vector<std::uint8_t> message{ 0x05, 0x00, 0x00, 0x01, 0x7F, 0x00, 0x00, 0x01, 0x04, 0x38 };
    Reply                           reply;
    Endpoint                        bound;

    ParseReplyOrFail(message, reply, bound);

    EXPECT_EQ(reply, Reply::Succeeded);
    EXPECT_EQ(bound.type, AddressType::IPv4);
    EXPECT_EQ(bound.host, "127.0.0.1");
    EXPECT_EQ(bound.port, 1080);
}

TEST(Unit_Socks5, ReadsTheDomainNameAndTheIpv6AddressOfAReply)
{
    const std::vector<std::uint8_t> named{ 0x05, 0x05, 0x00, 0x03, 0x03, 'f', 'o', 'o', 0x00, 0x50 };
    Reply                           reply;
    Endpoint                        bound;

    ParseReplyOrFail(named, reply, bound);
    EXPECT_EQ(reply, Reply::ConnectionRefused);
    EXPECT_EQ(bound.type, AddressType::DomainName);
    EXPECT_EQ(bound.host, "foo");
    EXPECT_EQ(bound.port, 80);

    std::vector<std::uint8_t> literal{ 0x05, 0x00, 0x00, 0x04 };
    literal.insert(literal.end(), 15, 0x00);
    literal.push_back(0x01);
    literal.push_back(0x1F);
    literal.push_back(0x90);

    ParseReplyOrFail(literal, reply, bound);
    EXPECT_EQ(bound.type, AddressType::IPv6);
    EXPECT_EQ(bound.host, "::1");
    EXPECT_EQ(bound.port, 8080);
}

TEST(Unit_Socks5, RefusesAnIncompleteOrForeignReply)
{
    const std::vector<std::uint8_t> header_only{ 0x05, 0x00, 0x00, 0x01 };
    const std::vector<std::uint8_t> truncated{ 0x05, 0x00, 0x00, 0x01, 0x7F, 0x00 };
    const std::vector<std::uint8_t> foreign{ 0x04, 0x00, 0x00, 0x01, 0x7F, 0x00, 0x00, 0x01, 0x04, 0x38 };
    const std::vector<std::uint8_t> unknown_type{ 0x05, 0x00, 0x00, 0x02, 0x00, 0x00 };
    Reply                           reply;
    Endpoint                        bound;

    EXPECT_FALSE(appbox::network::socks5::ParseReply(header_only.data(), header_only.size(), reply, bound));
    EXPECT_FALSE(appbox::network::socks5::ParseReply(truncated.data(), truncated.size(), reply, bound));
    EXPECT_FALSE(appbox::network::socks5::ParseReply(foreign.data(), foreign.size(), reply, bound));
    EXPECT_FALSE(appbox::network::socks5::ParseReply(unknown_type.data(), unknown_type.size(), reply, bound));
    EXPECT_FALSE(appbox::network::socks5::ParseReply(nullptr, 0, reply, bound));
}

TEST(Unit_Socks5, RendersAnAddressTheWayTheProtocolCarriesIt)
{
    const std::uint8_t ipv4[] = { 127, 0, 0, 1 };
    EXPECT_EQ(appbox::network::socks5::FormatAddress(AddressType::IPv4, ipv4, 4), "127.0.0.1");
    EXPECT_TRUE(appbox::network::socks5::FormatAddress(AddressType::IPv4, ipv4, 3).empty());

    /* ::1 */
    std::uint8_t ipv6[16] = {};
    ipv6[15] = 1;
    EXPECT_EQ(appbox::network::socks5::FormatAddress(AddressType::IPv6, ipv6, 16), "::1");

    /* 2001:db8::1 */
    std::uint8_t compressed[16] = { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
    EXPECT_EQ(appbox::network::socks5::FormatAddress(AddressType::IPv6, compressed, 16), "2001:db8::1");

    /* The longest run of zero groups is elided, even when a shorter one is first. */
    std::uint8_t longest[16] = { 0x00, 0x01, 0, 0, 0x00, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x03 };
    EXPECT_EQ(appbox::network::socks5::FormatAddress(AddressType::IPv6, longest, 16), "1:0:2::3");

    /* A run of a single group is not elided. */
    std::uint8_t single[16] = { 0x00, 0x01, 0, 0x02, 0, 0x03, 0, 0x04, 0, 0x05, 0, 0x06, 0, 0x07, 0, 0x08 };
    EXPECT_EQ(appbox::network::socks5::FormatAddress(AddressType::IPv6, single, 16), "1:2:3:4:5:6:7:8");

    EXPECT_TRUE(appbox::network::socks5::FormatAddress(AddressType::IPv6, ipv6, 15).empty());
    EXPECT_TRUE(appbox::network::socks5::FormatAddress(AddressType::DomainName, ipv6, 16).empty());
    EXPECT_TRUE(appbox::network::socks5::FormatAddress(AddressType::IPv4, nullptr, 4).empty());
}

TEST(Unit_Socks5, BuildsAndReadsTheDatagramOfAnAssociation)
{
    const std::uint8_t              payload[] = { 'h', 'i' };
    const Endpoint                  target = Literal("127.0.0.1", 9);
    const std::vector<std::uint8_t> expected{ 0x00, 0x00, 0x00, 0x01, 0x7F, 0x00, 0x00, 0x01, 0x00, 0x09, 'h', 'i' };

    const auto datagram = appbox::network::socks5::BuildUdpDatagram(target, payload, sizeof(payload));
    EXPECT_EQ(datagram, expected);

    Endpoint            sender;
    const std::uint8_t* read_payload = nullptr;
    std::size_t         read_size = 0;
    ASSERT_TRUE(
        appbox::network::socks5::ParseUdpDatagram(datagram.data(), datagram.size(), sender, read_payload, read_size));
    EXPECT_TRUE(appbox::network::socks5::EndpointsEqual(sender, target));
    ASSERT_EQ(read_size, sizeof(payload));
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(read_payload), read_size), "hi");
}

TEST(Unit_Socks5, BuildsAndReadsADatagramWithoutAPayload)
{
    const Endpoint target = Literal("::1", 5353);

    const auto datagram = appbox::network::socks5::BuildUdpDatagram(target, nullptr, 0);

    Endpoint            sender;
    const std::uint8_t* payload = nullptr;
    std::size_t         payload_size = 0;
    ASSERT_TRUE(
        appbox::network::socks5::ParseUdpDatagram(datagram.data(), datagram.size(), sender, payload, payload_size));
    EXPECT_EQ(sender.host, "::1");
    EXPECT_EQ(sender.port, 5353);
    EXPECT_EQ(payload_size, 0u);
}

TEST(Unit_Socks5, RefusesAFragmentedOrIncompleteDatagram)
{
    const std::vector<std::uint8_t> fragment{ 0x00, 0x00, 0x01, 0x01, 0x7F, 0x00, 0x00, 0x01, 0x00, 0x09 };
    const std::vector<std::uint8_t> reserved{ 0x00, 0x01, 0x00, 0x01, 0x7F, 0x00, 0x00, 0x01, 0x00, 0x09 };
    const std::vector<std::uint8_t> header_only{ 0x00, 0x00, 0x00, 0x01 };
    const std::vector<std::uint8_t> truncated{ 0x00, 0x00, 0x00, 0x01, 0x7F, 0x00, 0x00, 0x01, 0x00 };
    const std::vector<std::uint8_t> empty_name{ 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x09 };
    Endpoint                        sender;
    const std::uint8_t*             payload = nullptr;
    std::size_t                     payload_size = 0;

    EXPECT_FALSE(
        appbox::network::socks5::ParseUdpDatagram(fragment.data(), fragment.size(), sender, payload, payload_size));
    EXPECT_FALSE(
        appbox::network::socks5::ParseUdpDatagram(reserved.data(), reserved.size(), sender, payload, payload_size));
    EXPECT_FALSE(appbox::network::socks5::ParseUdpDatagram(header_only.data(), header_only.size(), sender, payload,
                                                           payload_size));
    EXPECT_FALSE(
        appbox::network::socks5::ParseUdpDatagram(truncated.data(), truncated.size(), sender, payload, payload_size));
    EXPECT_FALSE(
        appbox::network::socks5::ParseUdpDatagram(empty_name.data(), empty_name.size(), sender, payload, payload_size));
    EXPECT_FALSE(appbox::network::socks5::ParseUdpDatagram(nullptr, 0, sender, payload, payload_size));
}

TEST(Unit_Socks5, ComparesEndpointsByAddressAndPort)
{
    const Endpoint left = Literal("127.0.0.1", 1080);

    EXPECT_TRUE(appbox::network::socks5::EndpointsEqual(left, Literal("127.0.0.1", 1080)));
    EXPECT_FALSE(appbox::network::socks5::EndpointsEqual(left, Literal("127.0.0.1", 1081)));
    EXPECT_FALSE(appbox::network::socks5::EndpointsEqual(left, Literal("127.0.0.2", 1080)));
}
