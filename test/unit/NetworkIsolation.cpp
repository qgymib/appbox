#include <gtest/gtest.h>
#include "NetworkIsolation.hpp"
#include <cstdint>
#include <string>

namespace
{

/**
 * @brief Read the port of a text and fail the test when it is refused.
 * @param[in] text Text of the port.
 * @return The port of the text.
 */
std::uint16_t PortOf(const std::string& text)
{
    const auto port = appbox::network_isolation::ReadPortText(text);
    if (!port)
    {
        ADD_FAILURE() << "the port '" << text << "' was refused with the code " << static_cast<int>(port.error());
        return 0;
    }

    return *port;
}

/**
 * @brief Read the code which refused a port.
 * @param[in] text Text of the port.
 * @return The code of the refusal.
 */
appbox::ErrorCode PortRefusalOf(const std::string& text)
{
    const auto port = appbox::network_isolation::ReadPortText(text);
    if (port)
    {
        ADD_FAILURE() << "the port '" << text << "' was accepted as " << *port;
        return appbox::ErrorCode::kInvalidArgument;
    }

    return port.error();
}

/**
 * @brief Read an address and fail the test when it is refused.
 * @param[in] text Text of the address.
 * @return The address of the text.
 */
appbox::network_isolation::Address AddressOf(const std::string& text)
{
    const auto address = appbox::network_isolation::ParseAddress(text);
    if (!address)
    {
        ADD_FAILURE() << "the address '" << text << "' was refused with the code " << static_cast<int>(address.error());
        return {};
    }

    return *address;
}

} // namespace

/**
 * @brief A port is a decimal number between 1 and 65535 without a leading zero.
 *
 * The text is what the user entered in the workspace, so both the packer and the
 * sandbox read it with the same rules: a text which is not a port is never
 * silently read as another one.
 */
TEST(Unit_NetworkIsolation, PortTextFollowsTheRulesOfTheField)
{
    EXPECT_EQ(PortOf("1"), 1);
    EXPECT_EQ(PortOf("80"), 80);
    EXPECT_EQ(PortOf("1080"), 1080);
    EXPECT_EQ(PortOf("65535"), 65535);
}

/**
 * @brief Every rule which a port breaks is told apart by its code.
 *
 * The workspace reports each of the rules with its own wording, so the reader of
 * the text has to name the rule which failed.
 */
TEST(Unit_NetworkIsolation, PortRefusalNamesTheRule)
{
    using appbox::ErrorCode;

    EXPECT_EQ(PortRefusalOf(""), ErrorCode::kPortEmpty);
    EXPECT_EQ(PortRefusalOf("1080x"), ErrorCode::kPortNotDecimal);
    EXPECT_EQ(PortRefusalOf("1 080"), ErrorCode::kPortNotDecimal);
    EXPECT_EQ(PortRefusalOf("-1"), ErrorCode::kPortNotDecimal);
    EXPECT_EQ(PortRefusalOf("070"), ErrorCode::kPortLeadingZero);
    EXPECT_EQ(PortRefusalOf("00"), ErrorCode::kPortLeadingZero);
    EXPECT_EQ(PortRefusalOf("0"), ErrorCode::kPortOutOfRange);
    EXPECT_EQ(PortRefusalOf("65536"), ErrorCode::kPortOutOfRange);
    EXPECT_EQ(PortRefusalOf("999999"), ErrorCode::kPortOutOfRange);
}

/**
 * @brief A dotted quad is read into four bytes.
 */
TEST(Unit_NetworkIsolation, Ipv4AddressIsReadIntoItsBytes)
{
    const auto address = AddressOf("127.0.0.1");

    EXPECT_EQ(address.family, appbox::network_isolation::AddressFamily::IPv4);
    EXPECT_EQ(address.bytes[0], 127);
    EXPECT_EQ(address.bytes[1], 0);
    EXPECT_EQ(address.bytes[2], 0);
    EXPECT_EQ(address.bytes[3], 1);

    /* The remaining bytes of the buffer are untouched. */
    for (std::size_t index = 4; index < address.bytes.size(); ++index)
    {
        EXPECT_EQ(address.bytes[index], 0);
    }
}

/**
 * @brief A malformed address literal is refused with the code of the syntax.
 */
TEST(Unit_NetworkIsolation, MalformedAddressIsReportedByItsCode)
{
    using appbox::network_isolation::ParseAddress;
    using appbox::network_isolation::ParseIpv4;
    using appbox::network_isolation::ParseIpv6;
    using appbox::network_isolation::ParseIpv6Group;

    for (const char* text : { "", "127.0.0", "127.0.0.0.1", "127.0.0.256", "127.0.0.01", "127.0.0.", ".127.0.0.1",
                              "a.b.c.d", "127.0.0.1 ", ":" })
    {
        const auto address = ParseAddress(text);
        ASSERT_FALSE(address) << "address: '" << text << "'";
        EXPECT_EQ(address.error(), appbox::ErrorCode::kAddressMalformed);
    }

    for (const char* text : { "", "1.2.3", "1.2.3.4.5", ":::", "::1::2", "1:2:3:4:5:6:7:8:9", "fe80::1%eth0",
                              "12345::", "1:2:3", "::12345" })
    {
        const auto address = ParseIpv6(text);
        ASSERT_FALSE(address) << "address: '" << text << "'";
        EXPECT_EQ(address.error(), appbox::ErrorCode::kAddressMalformed);
    }

    const auto short_octets = ParseIpv4("1.2.3");
    ASSERT_FALSE(short_octets);
    EXPECT_EQ(short_octets.error(), appbox::ErrorCode::kAddressMalformed);

    const auto long_group = ParseIpv6Group("12345");
    ASSERT_FALSE(long_group);
    EXPECT_EQ(long_group.error(), appbox::ErrorCode::kAddressMalformed);

    const auto empty_group = ParseIpv6Group("");
    ASSERT_FALSE(empty_group);
    EXPECT_EQ(empty_group.error(), appbox::ErrorCode::kAddressMalformed);
}

/**
 * @brief The compressed and the embedded forms of an IPv6 address are read.
 */
TEST(Unit_NetworkIsolation, Ipv6AddressIsReadInItsForms)
{
    using appbox::network_isolation::AddressFamily;

    /* The full form. */
    const auto full = AddressOf("::1");
    EXPECT_EQ(full.family, AddressFamily::IPv6);
    for (std::size_t index = 0; index < 15; ++index)
    {
        EXPECT_EQ(full.bytes[index], 0);
    }
    EXPECT_EQ(full.bytes[15], 1);

    /* The compressed form elides the middle groups. */
    const auto compressed = AddressOf("2001:db8::ff00:42:8329");
    EXPECT_EQ(compressed.family, AddressFamily::IPv6);
    EXPECT_EQ(compressed.bytes[0], 0x20);
    EXPECT_EQ(compressed.bytes[1], 0x01);
    EXPECT_EQ(compressed.bytes[2], 0x0d);
    EXPECT_EQ(compressed.bytes[3], 0xb8);
    EXPECT_EQ(compressed.bytes[14], 0x83);
    EXPECT_EQ(compressed.bytes[15], 0x29);

    /* The last group can carry an embedded IPv4 address. */
    const auto embedded = AddressOf("::ffff:192.168.0.1");
    EXPECT_EQ(embedded.family, AddressFamily::IPv6);
    EXPECT_EQ(embedded.bytes[10], 0xff);
    EXPECT_EQ(embedded.bytes[11], 0xff);
    EXPECT_EQ(embedded.bytes[12], 192);
    EXPECT_EQ(embedded.bytes[13], 168);
    EXPECT_EQ(embedded.bytes[14], 0);
    EXPECT_EQ(embedded.bytes[15], 1);
}

/**
 * @brief A hostname is compared ignoring the case and the trailing dot.
 *
 * The name resolution of the host ignores both, so a redirection has to match
 * `Update.Example.COM.` and `update.example.com` alike.
 */
TEST(Unit_NetworkIsolation, HostnamesCompareAfterNormalization)
{
    using appbox::network_isolation::HostnamesEqual;
    using appbox::network_isolation::NormalizeHostname;

    EXPECT_EQ(NormalizeHostname("Update.Example.COM."), "update.example.com");
    EXPECT_EQ(NormalizeHostname(""), "");
    EXPECT_EQ(NormalizeHostname("."), ".");

    EXPECT_TRUE(HostnamesEqual("Update.Example.COM.", "update.example.com"));
    EXPECT_FALSE(HostnamesEqual("update.example.com", "update.example.org"));
}

/**
 * @brief The protocol token of the proxy is read ignoring the case.
 */
TEST(Unit_NetworkIsolation, Socks5TokenIsReadIgnoringTheCase)
{
    using appbox::network_isolation::IsSocks5Token;

    EXPECT_TRUE(IsSocks5Token("socks5"));
    EXPECT_TRUE(IsSocks5Token("SOCKS5"));
    EXPECT_TRUE(IsSocks5Token("Socks5"));
    EXPECT_FALSE(IsSocks5Token("socks4"));
    EXPECT_FALSE(IsSocks5Token(""));
    EXPECT_FALSE(IsSocks5Token("socks_5"));
}
