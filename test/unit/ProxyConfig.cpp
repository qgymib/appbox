#include <gtest/gtest.h>
#include "network/ProxyConfig.hpp"
#include "NetworkIsolation.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace
{

/**
 * @brief Build the text of a network isolation file which carries a proxy.
 * @param[in] proxy Object of the proxy member.
 * @return The text of the document.
 */
std::string FileWithProxy(const nlohmann::json& proxy)
{
    nlohmann::json document;
    document[appbox::network_isolation::kVersionKey] = appbox::network_isolation::kVersion;
    document[appbox::network_isolation::kEntriesKey] = nlohmann::json::array();
    document[appbox::network_isolation::kProxyKey] = proxy;
    return document.dump(2);
}

/**
 * @brief Build the text of a network isolation file which carries no proxy.
 * @return The text of the document.
 */
std::string FileWithoutProxy()
{
    nlohmann::json document;
    document[appbox::network_isolation::kVersionKey] = appbox::network_isolation::kVersion;
    document[appbox::network_isolation::kEntriesKey] = nlohmann::json::array();
    return document.dump(2);
}

/**
 * @brief Build the object of a complete SOCKS5 proxy.
 * @return The object of the proxy member.
 */
nlohmann::json Socks5Proxy()
{
    nlohmann::json proxy;
    proxy[appbox::network_isolation::kProxyTypeKey] = appbox::network_isolation::kSocks5Token;
    proxy[appbox::network_isolation::kProxyTcpKey] = true;
    proxy[appbox::network_isolation::kProxyUdpKey] = true;
    proxy[appbox::network_isolation::kProxyServerKey] = "127.0.0.1";
    proxy[appbox::network_isolation::kProxyPortKey] = "1080";
    proxy[appbox::network_isolation::kProxyUsernameKey] = "user";
    proxy[appbox::network_isolation::kProxyPasswordKey] = "secret";
    return proxy;
}

/**
 * @brief Read the proxy of a document and fail the test when it is rejected.
 * @param[in] text Text of the document.
 * @return The proxy of the document.
 */
appbox::network::ProxyConfig ParseOrFail(const std::string& text)
{
    appbox::network::ProxyConfig config;
    EXPECT_TRUE(appbox::network::ParseProxyConfig(text, config)) << text;
    return config;
}

} // namespace

TEST(Unit_ProxyConfig, ReadsTheProxyOfTheFile)
{
    const auto config = ParseOrFail(FileWithProxy(Socks5Proxy()));

    EXPECT_TRUE(config.tcp);
    EXPECT_TRUE(config.udp);
    EXPECT_EQ(config.server, "127.0.0.1");
    EXPECT_EQ(config.port, 1080);
    EXPECT_EQ(config.username, "user");
    EXPECT_EQ(config.password, "secret");
    EXPECT_TRUE(config.IsEnabled());
    EXPECT_TRUE(config.HasCredentials());
}

TEST(Unit_ProxyConfig, ReadsTheFlagsOfTheFile)
{
    auto proxy = Socks5Proxy();
    proxy[appbox::network_isolation::kProxyTcpKey] = false;
    proxy[appbox::network_isolation::kProxyUdpKey] = true;

    const auto config = ParseOrFail(FileWithProxy(proxy));

    EXPECT_FALSE(config.tcp);
    EXPECT_TRUE(config.udp);
}

TEST(Unit_ProxyConfig, ReadsAProxyWithoutCredentials)
{
    auto proxy = Socks5Proxy();
    proxy.erase(appbox::network_isolation::kProxyUsernameKey);
    proxy.erase(appbox::network_isolation::kProxyPasswordKey);

    const auto config = ParseOrFail(FileWithProxy(proxy));

    EXPECT_TRUE(config.tcp);
    EXPECT_FALSE(config.HasCredentials());
}

TEST(Unit_ProxyConfig, ReportsNoProxyWithoutTheMember)
{
    const auto config = ParseOrFail(FileWithoutProxy());

    EXPECT_FALSE(config.IsEnabled());
    EXPECT_TRUE(config.server.empty());
    EXPECT_EQ(config.port, 0);
}

TEST(Unit_ProxyConfig, ReportsNoProxyForAProxyWhichIsNotAnObject)
{
    const auto config = ParseOrFail(FileWithProxy(nlohmann::json("socks5")));

    EXPECT_FALSE(config.IsEnabled());
}

TEST(Unit_ProxyConfig, ReportsNoProxyForAnotherProtocol)
{
    auto proxy = Socks5Proxy();
    proxy[appbox::network_isolation::kProxyTypeKey] = "http";

    const auto config = ParseOrFail(FileWithProxy(proxy));

    EXPECT_FALSE(config.IsEnabled());
}

TEST(Unit_ProxyConfig, ReportsNoProxyForAnEmptyServer)
{
    auto proxy = Socks5Proxy();
    proxy[appbox::network_isolation::kProxyServerKey] = "";

    const auto config = ParseOrFail(FileWithProxy(proxy));

    EXPECT_FALSE(config.IsEnabled());
}

TEST(Unit_ProxyConfig, ReportsNoProxyForAPortThePackerWouldRefuse)
{
    const char* ports[] = { "0", "070", "65536", "1080x", "" };
    for (const char* port : ports)
    {
        auto proxy = Socks5Proxy();
        proxy[appbox::network_isolation::kProxyPortKey] = port;

        const auto config = ParseOrFail(FileWithProxy(proxy));

        EXPECT_FALSE(config.IsEnabled()) << "port: '" << port << "'";
    }
}

TEST(Unit_ProxyConfig, ReportsNoProxyForAMemberOfAnotherType)
{
    auto proxy = Socks5Proxy();
    proxy[appbox::network_isolation::kProxyTcpKey] = "yes";
    proxy[appbox::network_isolation::kProxyUdpKey] = "no";

    const auto config = ParseOrFail(FileWithProxy(proxy));

    /* A member of another type leaves its flag clear, so no traffic is carried. */
    EXPECT_FALSE(config.tcp);
    EXPECT_FALSE(config.udp);
    EXPECT_FALSE(config.IsEnabled());
}

TEST(Unit_ProxyConfig, RejectsADocumentOfAnotherSchema)
{
    nlohmann::json document = nlohmann::json::parse(FileWithProxy(Socks5Proxy()));
    document[appbox::network_isolation::kVersionKey] = 2;

    appbox::network::ProxyConfig config;
    EXPECT_FALSE(appbox::network::ParseProxyConfig(document.dump(2), config));
    EXPECT_FALSE(config.IsEnabled());
}

TEST(Unit_ProxyConfig, RejectsADocumentWhichIsNotAnObject)
{
    appbox::network::ProxyConfig config;
    EXPECT_FALSE(appbox::network::ParseProxyConfig("[]", config));
    EXPECT_FALSE(config.IsEnabled());
    EXPECT_FALSE(appbox::network::ParseProxyConfig("{", config));
    EXPECT_FALSE(config.IsEnabled());
}

TEST(Unit_ProxyConfig, DescribesAConfigurationWithoutTheCredentials)
{
    const auto config = ParseOrFail(FileWithProxy(Socks5Proxy()));

    const auto text = appbox::network::DescribeProxy(config);

    EXPECT_EQ(text, "socks5 tcp+udp 127.0.0.1:1080 (credentials)");
    EXPECT_EQ(text.find("secret"), std::string::npos);
}

TEST(Unit_ProxyConfig, DescribesOneProtocolAndADisabledProxy)
{
    auto proxy = Socks5Proxy();
    proxy[appbox::network_isolation::kProxyUdpKey] = false;

    EXPECT_EQ(appbox::network::DescribeProxy(ParseOrFail(FileWithProxy(proxy))),
              "socks5 tcp 127.0.0.1:1080 (credentials)");
    EXPECT_EQ(appbox::network::DescribeProxy(appbox::network::ProxyConfig{}), "none");
}
