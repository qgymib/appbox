#include <gtest/gtest.h>
#include "NetworkIsolation.hpp"
#include <string>

namespace
{

/**
 * @brief Parse an address literal and fail the test when it is refused.
 * @param[in] text Text to parse.
 * @return The parsed address.
 */
appbox::network_isolation::Address ParseOrFail(const std::string& text)
{
    appbox::network_isolation::Address address;
    EXPECT_TRUE(appbox::network_isolation::ParseAddress(text, address)) << text;
    return address;
}

/**
 * @brief Check that a text is refused as an address literal.
 * @param[in] text Text to parse.
 */
void ExpectRefused(const std::string& text)
{
    appbox::network_isolation::Address address;
    EXPECT_FALSE(appbox::network_isolation::ParseAddress(text, address)) << text;
}

} // namespace

TEST(UnitNetworkIsolation, ParsesAnIpv4Address)
{
    const auto address = ParseOrFail("127.0.0.1");

    EXPECT_EQ(address.family, appbox::network_isolation::AddressFamily::IPv4);
    EXPECT_EQ(address.bytes[0], 127);
    EXPECT_EQ(address.bytes[1], 0);
    EXPECT_EQ(address.bytes[2], 0);
    EXPECT_EQ(address.bytes[3], 1);
}

TEST(UnitNetworkIsolation, ParsesTheBoundariesOfAnIpv4Address)
{
    const auto lowest = ParseOrFail("0.0.0.0");
    EXPECT_EQ(lowest.bytes[0], 0);
    EXPECT_EQ(lowest.bytes[3], 0);

    const auto highest = ParseOrFail("255.255.255.255");
    EXPECT_EQ(highest.bytes[0], 255);
    EXPECT_EQ(highest.bytes[3], 255);
}

TEST(UnitNetworkIsolation, RefusesAMalformedIpv4Address)
{
    ExpectRefused("");
    ExpectRefused("127.0.0");
    ExpectRefused("127.0.0.1.5");
    ExpectRefused("127.0.0.");
    ExpectRefused(".127.0.0.1");
    ExpectRefused("127..0.1");
    ExpectRefused("256.0.0.1");
    ExpectRefused("127.0.0.1000");
    ExpectRefused("127.0.0.-1");
    ExpectRefused("127.0.0.1x");
    ExpectRefused("a.b.c.d");
    ExpectRefused("127.0.0.1 ");
    ExpectRefused(" 127.0.0.1");
}

TEST(UnitNetworkIsolation, RefusesAnIpv4OctetWithALeadingZero)
{
    /* A leading zero would be read as an octal number by some parsers. */
    ExpectRefused("010.0.0.1");
    ExpectRefused("127.0.0.01");
}

TEST(UnitNetworkIsolation, ParsesAFullIpv6Address)
{
    const auto address = ParseOrFail("2001:0db8:0000:0000:0000:ff00:0042:8329");

    EXPECT_EQ(address.family, appbox::network_isolation::AddressFamily::IPv6);
    EXPECT_EQ(address.bytes[0], 0x20);
    EXPECT_EQ(address.bytes[1], 0x01);
    EXPECT_EQ(address.bytes[15], 0x29);
}

TEST(UnitNetworkIsolation, ParsesACompressedIpv6Address)
{
    const auto loopback = ParseOrFail("::1");
    EXPECT_EQ(loopback.family, appbox::network_isolation::AddressFamily::IPv6);
    for (std::size_t index = 0; index < 15; ++index)
    {
        EXPECT_EQ(loopback.bytes[index], 0) << index;
    }
    EXPECT_EQ(loopback.bytes[15], 1);

    const auto link_local = ParseOrFail("fe80::1");
    EXPECT_EQ(link_local.bytes[0], 0xfe);
    EXPECT_EQ(link_local.bytes[1], 0x80);
    EXPECT_EQ(link_local.bytes[15], 1);

    const auto unspecified = ParseOrFail("::");
    for (const auto byte : unspecified.bytes)
    {
        EXPECT_EQ(byte, 0);
    }
}

TEST(UnitNetworkIsolation, ParsesAnIpv6AddressWithAnEmbeddedIpv4Address)
{
    const auto address = ParseOrFail("::ffff:127.0.0.1");

    EXPECT_EQ(address.family, appbox::network_isolation::AddressFamily::IPv6);
    for (std::size_t index = 0; index < 10; ++index)
    {
        EXPECT_EQ(address.bytes[index], 0) << index;
    }
    EXPECT_EQ(address.bytes[10], 0xff);
    EXPECT_EQ(address.bytes[11], 0xff);
    EXPECT_EQ(address.bytes[12], 127);
    EXPECT_EQ(address.bytes[15], 1);
}

TEST(UnitNetworkIsolation, RefusesAMalformedIpv6Address)
{
    ExpectRefused(":");
    ExpectRefused(":::");
    ExpectRefused("1:2:3:4:5:6:7");
    ExpectRefused("1:2:3:4:5:6:7:8:9");
    ExpectRefused("1:2:3:4:5:6:7:");
    ExpectRefused("1::2::3");
    ExpectRefused("1:2:3:4:5:6:7::8");
    ExpectRefused("12345::1");
    ExpectRefused("fe80::1%12");
    ExpectRefused("1:2:3:4:5:6:1.2.3");
}

TEST(UnitNetworkIsolation, NormalizesAHostnameForTheComparison)
{
    EXPECT_EQ(appbox::network_isolation::NormalizeHostname("Example.COM"), "example.com");
    EXPECT_EQ(appbox::network_isolation::NormalizeHostname("example.com."), "example.com");
    EXPECT_EQ(appbox::network_isolation::NormalizeHostname("Example.COM."), "example.com");
    EXPECT_EQ(appbox::network_isolation::NormalizeHostname("example.com"), "example.com");
    EXPECT_EQ(appbox::network_isolation::NormalizeHostname("."), ".");
    EXPECT_EQ(appbox::network_isolation::NormalizeHostname(""), "");
    EXPECT_EQ(appbox::network_isolation::NormalizeHostname("127.0.0.1"), "127.0.0.1");
}

TEST(UnitNetworkIsolation, ComparesHostnamesIgnoringTheCaseAndATrailingDot)
{
    EXPECT_TRUE(appbox::network_isolation::HostnamesEqual("Example.COM", "example.com"));
    EXPECT_TRUE(appbox::network_isolation::HostnamesEqual("example.com.", "example.com"));
    EXPECT_TRUE(appbox::network_isolation::HostnamesEqual("EXAMPLE.com.", "example.COM"));
    EXPECT_FALSE(appbox::network_isolation::HostnamesEqual("example.com", "example.org"));
    EXPECT_FALSE(appbox::network_isolation::HostnamesEqual("example.com", "www.example.com"));
}
