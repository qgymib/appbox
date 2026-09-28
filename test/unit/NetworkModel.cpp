#include <gtest/gtest.h>
#include "src/core/NetworkModel.hpp"
#include <string>
#include <vector>

namespace
{

/**
 * @brief Add a DNS redirection and fail the test when the model refuses it.
 * @param[in,out] model Model to update.
 * @param[in] hostname Hostname of the entry.
 * @param[in] redirect Redirect target of the entry.
 */
void AddEntry(appbox::NetworkModel& model, const std::wstring& hostname, const std::wstring& redirect)
{
    std::string error;
    ASSERT_TRUE(model.AddDnsEntry(hostname, redirect, error)) << error;
}

/**
 * @brief Take a copy of the entries of a model.
 * @param[in] model Model to read.
 * @return The entries of the model.
 */
std::vector<appbox::DnsRedirectEntry> Snapshot(const appbox::NetworkModel& model)
{
    return model.DnsEntries();
}

/**
 * @brief Compare the entries of a model with a snapshot.
 * @param[in] model Model to read.
 * @param[in] expected Entries the model is expected to hold.
 */
void ExpectEntries(const appbox::NetworkModel& model, const std::vector<appbox::DnsRedirectEntry>& expected)
{
    const auto& entries = model.DnsEntries();
    ASSERT_EQ(entries.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(entries[index].hostname, expected[index].hostname) << "entry " << index;
        EXPECT_EQ(entries[index].redirect, expected[index].redirect) << "entry " << index;
    }
}

/**
 * @brief Build a proxy configuration with a server, a port and credentials.
 * @param[in] tcp Whether the TCP traffic is proxied.
 * @param[in] udp Whether the UDP traffic is proxied.
 * @return The configuration.
 */
appbox::ProxyConfig MakeProxy(bool tcp, bool udp)
{
    appbox::ProxyConfig proxy;
    proxy.type = appbox::ProxyType::Socks5;
    proxy.tcp = tcp;
    proxy.udp = udp;
    proxy.server = L"127.0.0.1";
    proxy.port = L"1080";
    proxy.username = L"user";
    proxy.password = L"secret";
    return proxy;
}

/**
 * @brief Store a proxy configuration and fail the test when it is refused.
 * @param[in,out] model Model to update.
 * @param[in] proxy Configuration to store.
 */
void StoreProxy(appbox::NetworkModel& model, const appbox::ProxyConfig& proxy)
{
    std::string error;
    ASSERT_TRUE(model.SetProxy(proxy, error)) << error;
}

/**
 * @brief Compare the proxy of a model with an expected configuration.
 * @param[in] model Model to read.
 * @param[in] expected Configuration the model is expected to hold.
 */
void ExpectProxy(const appbox::NetworkModel& model, const appbox::ProxyConfig& expected)
{
    const auto& proxy = model.Proxy();
    EXPECT_EQ(proxy.type, expected.type);
    EXPECT_EQ(proxy.tcp, expected.tcp);
    EXPECT_EQ(proxy.udp, expected.udp);
    EXPECT_EQ(proxy.server, expected.server);
    EXPECT_EQ(proxy.port, expected.port);
    EXPECT_EQ(proxy.username, expected.username);
    EXPECT_EQ(proxy.password, expected.password);
}

} // namespace

TEST(Unit_NetworkModel, StartsEmpty)
{
    appbox::NetworkModel model;

    EXPECT_TRUE(model.IsEmpty());
    EXPECT_TRUE(model.DnsEntries().empty());
    EXPECT_EQ(model.IndexOfHostname(L"example.com"), -1);
}

TEST(Unit_NetworkModel, AddsEntriesInInsertionOrder)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    AddEntry(model, L"api.example.com", L"10.0.0.1");
    AddEntry(model, L"192.168.0.1", L"192.168.0.2");

    EXPECT_FALSE(model.IsEmpty());
    ExpectEntries(model, {
                             { L"example.com",     L"127.0.0.1"   },
                             { L"api.example.com", L"10.0.0.1"    },
                             { L"192.168.0.1",     L"192.168.0.2" }
    });
}

TEST(Unit_NetworkModel, IndexOfHostnameIgnoresTheCase)
{
    appbox::NetworkModel model;
    AddEntry(model, L"Example.COM", L"127.0.0.1");

    EXPECT_EQ(model.IndexOfHostname(L"example.com"), 0);
    EXPECT_EQ(model.IndexOfHostname(L"EXAMPLE.COM"), 0);
    EXPECT_EQ(model.IndexOfHostname(L"other.example.com"), -1);
}

TEST(Unit_NetworkModel, RefusesEmptyFields)
{
    appbox::NetworkModel model;
    std::string          error;

    EXPECT_FALSE(model.AddDnsEntry(L"", L"127.0.0.1", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddDnsEntry(L"example.com", L"", error));
    EXPECT_FALSE(error.empty());

    EXPECT_TRUE(model.IsEmpty());
}

TEST(Unit_NetworkModel, RefusesWhitespaceInFields)
{
    appbox::NetworkModel model;
    std::string          error;

    EXPECT_FALSE(model.AddDnsEntry(L"   ", L"127.0.0.1", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddDnsEntry(L"example.com", L"\t", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddDnsEntry(L"example .com", L"127.0.0.1", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddDnsEntry(L"example.com", L"127.0.0.1 ", error));
    EXPECT_FALSE(error.empty());

    EXPECT_TRUE(model.IsEmpty());
}

TEST(Unit_NetworkModel, RefusesADuplicateHostnameIgnoringTheCase)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");

    std::string error;
    EXPECT_FALSE(model.AddDnsEntry(L"EXAMPLE.COM", L"127.0.0.2", error));
    EXPECT_FALSE(error.empty());
    ExpectEntries(model, {
                             { L"example.com", L"127.0.0.1" }
    });
}

TEST(Unit_NetworkModel, ARefusedAddLeavesTheModelUntouched)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    const auto before = Snapshot(model);

    std::string error;
    EXPECT_FALSE(model.AddDnsEntry(L"", L"10.0.0.1", error));
    EXPECT_FALSE(model.AddDnsEntry(L"api.example.com", L" ", error));
    EXPECT_FALSE(model.AddDnsEntry(L"example.com", L"10.0.0.1", error));

    ExpectEntries(model, before);
}

TEST(Unit_NetworkModel, SetReplacesTheEntryInPlace)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    AddEntry(model, L"api.example.com", L"10.0.0.1");

    std::string error;
    ASSERT_TRUE(model.SetDnsEntry(0, L"www.example.com", L"127.0.0.9", error)) << error;

    ExpectEntries(model, {
                             { L"www.example.com", L"127.0.0.9" },
                             { L"api.example.com", L"10.0.0.1"  }
    });
}

TEST(Unit_NetworkModel, SetAllowsRecasingItsOwnHostname)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");

    std::string error;
    ASSERT_TRUE(model.SetDnsEntry(0, L"EXAMPLE.COM", L"127.0.0.1", error)) << error;
    ExpectEntries(model, {
                             { L"EXAMPLE.COM", L"127.0.0.1" }
    });
}

TEST(Unit_NetworkModel, SetRefusesTheHostnameOfAnotherEntry)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    AddEntry(model, L"api.example.com", L"10.0.0.1");

    std::string error;
    EXPECT_FALSE(model.SetDnsEntry(1, L"Example.com", L"10.0.0.1", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.SetDnsEntry(0, L"API.EXAMPLE.COM", L"127.0.0.1", error));
    EXPECT_FALSE(error.empty());

    ExpectEntries(model, {
                             { L"example.com",     L"127.0.0.1" },
                             { L"api.example.com", L"10.0.0.1"  }
    });
}

TEST(Unit_NetworkModel, SetRefusesEmptyAndWhitespaceFields)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");

    std::string error;
    EXPECT_FALSE(model.SetDnsEntry(0, L"", L"127.0.0.1", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.SetDnsEntry(0, L"example.com", L"", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.SetDnsEntry(0, L"exam ple.com", L"127.0.0.1", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.SetDnsEntry(0, L"example.com", L"127.0.0.1\n", error));
    EXPECT_FALSE(error.empty());

    ExpectEntries(model, {
                             { L"example.com", L"127.0.0.1" }
    });
}

TEST(Unit_NetworkModel, SetRefusesAnIndexOutsideTheModel)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");

    std::string error;
    EXPECT_FALSE(model.SetDnsEntry(1, L"api.example.com", L"10.0.0.1", error));
    EXPECT_FALSE(error.empty());

    ExpectEntries(model, {
                             { L"example.com", L"127.0.0.1" }
    });
}

TEST(Unit_NetworkModel, RemoveDropsTheEntryAtTheIndex)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    AddEntry(model, L"api.example.com", L"10.0.0.1");
    AddEntry(model, L"192.168.0.1", L"192.168.0.2");

    ASSERT_TRUE(model.RemoveDnsEntry(1));

    ExpectEntries(model, {
                             { L"example.com", L"127.0.0.1"   },
                             { L"192.168.0.1", L"192.168.0.2" }
    });
}

TEST(Unit_NetworkModel, RemoveRefusesAnIndexOutsideTheModel)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");

    EXPECT_FALSE(model.RemoveDnsEntry(1));
    ExpectEntries(model, {
                             { L"example.com", L"127.0.0.1" }
    });
}

TEST(Unit_NetworkModel, ResetDropsEveryEntry)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    AddEntry(model, L"api.example.com", L"10.0.0.1");

    model.Reset();

    EXPECT_TRUE(model.IsEmpty());
    EXPECT_TRUE(model.DnsEntries().empty());
}

TEST(Unit_NetworkModel, AcceptsAnIpv4AndAnIpv6Redirect)
{
    appbox::NetworkModel model;
    AddEntry(model, L"v4.example.com", L"127.0.0.1");
    AddEntry(model, L"v6.example.com", L"::1");
    AddEntry(model, L"full.example.com", L"2001:db8::1");

    ExpectEntries(model, {
                             { L"v4.example.com",   L"127.0.0.1"   },
                             { L"v6.example.com",   L"::1"         },
                             { L"full.example.com", L"2001:db8::1" }
    });
}

TEST(Unit_NetworkModel, RefusesARedirectWhichIsNotAnAddress)
{
    appbox::NetworkModel model;
    std::string          error;

    EXPECT_FALSE(model.AddDnsEntry(L"example.com", L"host.example.com", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddDnsEntry(L"example.com", L"127.0.0", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddDnsEntry(L"example.com", L"256.0.0.1", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddDnsEntry(L"example.com", L"010.0.0.1", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddDnsEntry(L"example.com", L"fe80::1%12", error));
    EXPECT_FALSE(error.empty());

    EXPECT_TRUE(model.IsEmpty());
}

TEST(Unit_NetworkModel, SetRefusesARedirectWhichIsNotAnAddress)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");

    std::string error;
    EXPECT_FALSE(model.SetDnsEntry(0, L"example.com", L"not-an-address", error));
    EXPECT_FALSE(error.empty());

    ExpectEntries(model, {
                             { L"example.com", L"127.0.0.1" }
    });
}

TEST(Unit_NetworkModel, RefusesADuplicateHostnameWithATrailingDot)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");

    std::string error;
    EXPECT_FALSE(model.AddDnsEntry(L"example.com.", L"127.0.0.2", error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(model.IndexOfHostname(L"example.com."), 0);

    ExpectEntries(model, {
                             { L"example.com", L"127.0.0.1" }
    });
}

TEST(Unit_NetworkModel, StartsWithoutAProxy)
{
    appbox::NetworkModel model;

    ExpectProxy(model, appbox::ProxyConfig{});
    EXPECT_FALSE(model.HasProxy());
    EXPECT_TRUE(model.IsEmpty());
}

TEST(Unit_NetworkModel, StoresACompleteProxyConfiguration)
{
    appbox::NetworkModel model;
    const auto           proxy = MakeProxy(true, true);

    StoreProxy(model, proxy);

    ExpectProxy(model, proxy);
    EXPECT_TRUE(model.HasProxy());
    EXPECT_FALSE(model.IsEmpty());
}

TEST(Unit_NetworkModel, StoresAProxyWhichCarriesOneProtocolOnly)
{
    appbox::NetworkModel model;
    const auto           proxy = MakeProxy(true, false);

    StoreProxy(model, proxy);

    ExpectProxy(model, proxy);
    EXPECT_TRUE(model.Proxy().tcp);
    EXPECT_FALSE(model.Proxy().udp);
}

TEST(Unit_NetworkModel, KeepsTheFieldsWhileNoProtocolIsEnabled)
{
    appbox::NetworkModel model;
    auto                 proxy = MakeProxy(false, false);

    StoreProxy(model, proxy);

    /* The configuration is remembered, so turning the proxy off loses nothing. */
    ExpectProxy(model, proxy);
    EXPECT_TRUE(model.HasProxy());
    EXPECT_FALSE(model.IsEmpty());
}

TEST(Unit_NetworkModel, StoresAProxyWithoutCredentials)
{
    appbox::NetworkModel model;
    auto                 proxy = MakeProxy(true, false);
    proxy.username.clear();
    proxy.password.clear();

    StoreProxy(model, proxy);

    ExpectProxy(model, proxy);
    EXPECT_TRUE(model.Proxy().username.empty());
    EXPECT_TRUE(model.Proxy().password.empty());
}

TEST(Unit_NetworkModel, AcceptsCredentialsWithWhitespace)
{
    appbox::NetworkModel model;
    auto                 proxy = MakeProxy(true, false);
    proxy.username = L"user name";
    proxy.password = L"p a s s word";

    StoreProxy(model, proxy);

    ExpectProxy(model, proxy);
}

TEST(Unit_NetworkModel, RefusesAProtocolWithoutAServer)
{
    appbox::NetworkModel model;
    auto                 proxy = MakeProxy(false, false);
    proxy.server.clear();
    proxy.tcp = true;

    std::string error;
    EXPECT_FALSE(model.SetProxy(proxy, error));
    EXPECT_FALSE(error.empty());

    proxy.tcp = false;
    proxy.udp = true;
    error.clear();
    EXPECT_FALSE(model.SetProxy(proxy, error));
    EXPECT_FALSE(error.empty());

    EXPECT_FALSE(model.HasProxy());
    EXPECT_TRUE(model.IsEmpty());
}

TEST(Unit_NetworkModel, RefusesAProtocolWithoutAPort)
{
    appbox::NetworkModel model;
    auto                 proxy = MakeProxy(true, false);
    proxy.port.clear();

    std::string error;
    EXPECT_FALSE(model.SetProxy(proxy, error));
    EXPECT_FALSE(error.empty());

    proxy.tcp = false;
    proxy.udp = true;
    error.clear();
    EXPECT_FALSE(model.SetProxy(proxy, error));
    EXPECT_FALSE(error.empty());

    EXPECT_FALSE(model.HasProxy());
}

TEST(Unit_NetworkModel, RefusesAServerWithAWhitespaceCharacter)
{
    appbox::NetworkModel model;
    auto                 proxy = MakeProxy(true, false);
    proxy.server = L"127.0.0.1 ";

    std::string error;
    EXPECT_FALSE(model.SetProxy(proxy, error));
    EXPECT_FALSE(error.empty());

    proxy.server = L"proxy server";
    error.clear();
    EXPECT_FALSE(model.SetProxy(proxy, error));
    EXPECT_FALSE(error.empty());

    EXPECT_FALSE(model.HasProxy());
}

TEST(Unit_NetworkModel, RefusesAPortWhichIsNotAPortNumber)
{
    appbox::NetworkModel model;
    auto                 proxy = MakeProxy(false, false);

    std::string error;
    proxy.port = L"abc";
    EXPECT_FALSE(model.SetProxy(proxy, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    proxy.port = L"10 80";
    EXPECT_FALSE(model.SetProxy(proxy, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    proxy.port = L"0";
    EXPECT_FALSE(model.SetProxy(proxy, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    proxy.port = L"65536";
    EXPECT_FALSE(model.SetProxy(proxy, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    proxy.port = L"01080";
    EXPECT_FALSE(model.SetProxy(proxy, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    proxy.port = L"123456";
    EXPECT_FALSE(model.SetProxy(proxy, error));
    EXPECT_FALSE(error.empty());

    /* An empty port is the port which was not entered yet. */
    error.clear();
    proxy.port.clear();
    EXPECT_TRUE(model.SetProxy(proxy, error)) << error;
}

TEST(Unit_NetworkModel, AcceptsTheLowestAndTheHighestPort)
{
    appbox::NetworkModel model;
    auto                 proxy = MakeProxy(true, false);

    proxy.port = L"1";
    StoreProxy(model, proxy);
    EXPECT_EQ(model.Proxy().port, L"1");

    proxy.port = L"65535";
    StoreProxy(model, proxy);
    EXPECT_EQ(model.Proxy().port, L"65535");
}

TEST(Unit_NetworkModel, ARefusedProxyLeavesTheModelUntouched)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    const auto stored = MakeProxy(true, false);
    StoreProxy(model, stored);

    const auto before = Snapshot(model);

    auto proxy = MakeProxy(true, false);
    proxy.port = L"0";

    std::string error;
    EXPECT_FALSE(model.SetProxy(proxy, error));
    EXPECT_FALSE(error.empty());

    ExpectProxy(model, stored);
    ExpectEntries(model, before);
}

TEST(Unit_NetworkModel, ResetDropsTheProxyAsWell)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    StoreProxy(model, MakeProxy(true, true));

    model.Reset();

    ExpectProxy(model, appbox::ProxyConfig{});
    EXPECT_FALSE(model.HasProxy());
    EXPECT_TRUE(model.IsEmpty());
}

TEST(Unit_NetworkModel, ProxyTypeTokensRoundTrip)
{
    EXPECT_STREQ(appbox::ProxyTypeToken(appbox::ProxyType::Socks5), "socks5");

    appbox::ProxyType type = appbox::ProxyType::Socks5;
    EXPECT_TRUE(appbox::ParseProxyTypeToken("socks5", type));
    EXPECT_EQ(type, appbox::ProxyType::Socks5);
    EXPECT_TRUE(appbox::ParseProxyTypeToken("SOCKS5", type));
    EXPECT_EQ(type, appbox::ProxyType::Socks5);
    EXPECT_FALSE(appbox::ParseProxyTypeToken("socks4", type));
    EXPECT_FALSE(appbox::ParseProxyTypeToken("", type));
}
