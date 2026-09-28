#include <gtest/gtest.h>
#include "src/core/NetworkIsolationFile.hpp"
#include "src/core/NetworkModel.hpp"
#include <nlohmann/json.hpp>
#include <string>

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
 * @brief Set the proxy of a model and fail the test when it refuses it.
 * @param[in,out] model Model to update.
 * @param[in] proxy Proxy configuration to store.
 */
void SetProxy(appbox::NetworkModel& model, const appbox::ProxyConfig& proxy)
{
    std::string error;
    ASSERT_TRUE(model.SetProxy(proxy, error)) << error;
}

/**
 * @brief Build the isolation file of a model and fail the test on failure.
 * @param[in] model Model to describe.
 * @return The text of the isolation file.
 */
std::string BuildOrFail(const appbox::NetworkModel& model)
{
    std::string text;
    std::string error;
    EXPECT_TRUE(appbox::BuildNetworkIsolationFile(model, text, error)) << error;
    return text;
}

} // namespace

TEST(Unit_NetworkIsolationFile, WritesTheSchemaOfAnEmptyModel)
{
    appbox::NetworkModel model;

    const auto document = nlohmann::json::parse(BuildOrFail(model));

    EXPECT_EQ(document["version"], 1);
    ASSERT_TRUE(document["entries"].is_array());
    EXPECT_TRUE(document["entries"].empty());
}

TEST(Unit_NetworkIsolationFile, WritesEveryEntryInModelOrder)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");
    AddEntry(model, L"api.example.com", L"::1");

    const auto document = nlohmann::json::parse(BuildOrFail(model));

    ASSERT_EQ(document["entries"].size(), 2u);
    EXPECT_EQ(document["entries"][0]["hostname"], "example.com");
    EXPECT_EQ(document["entries"][0]["redirect"], "127.0.0.1");
    EXPECT_EQ(document["entries"][1]["hostname"], "api.example.com");
    EXPECT_EQ(document["entries"][1]["redirect"], "::1");
}

TEST(Unit_NetworkIsolationFile, KeepsTheTextStableForAGivenModel)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");

    EXPECT_EQ(BuildOrFail(model), BuildOrFail(model));
}

TEST(Unit_NetworkIsolationFile, WritesTheHostnameAsUTF8)
{
    appbox::NetworkModel model;
    AddEntry(model, L"caf\u00e9.example", L"10.0.0.1");

    const auto document = nlohmann::json::parse(BuildOrFail(model));

    EXPECT_EQ(document["entries"][0]["hostname"], "caf\xc3\xa9.example");
}

TEST(Unit_NetworkIsolationFile, RejectsAnEntryTheModelWouldRefuse)
{
    appbox::NetworkModel model;

    std::string error;
    EXPECT_FALSE(model.AddDnsEntry(L"example.com", L"not-an-address", error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(model.IsEmpty());
}

TEST(Unit_NetworkIsolationFile, WritesTheProxyWhileTheModelHoldsOne)
{
    appbox::NetworkModel model;
    appbox::ProxyConfig  proxy;
    proxy.tcp = true;
    proxy.server = L"proxy.example";
    proxy.port = L"1080";
    proxy.username = L"user";
    proxy.password = L"secret";
    SetProxy(model, proxy);

    const auto document = nlohmann::json::parse(BuildOrFail(model));

    ASSERT_TRUE(document.contains("proxy"));
    const auto& item = document["proxy"];
    EXPECT_EQ(item["type"], "socks5");
    EXPECT_EQ(item["tcp"], true);
    EXPECT_EQ(item["udp"], false);
    EXPECT_EQ(item["server"], "proxy.example");
    EXPECT_EQ(item["port"], "1080");
    EXPECT_EQ(item["username"], "user");
    EXPECT_EQ(item["password"], "secret");
}

TEST(Unit_NetworkIsolationFile, WritesTheProxyWhileOnlyAFieldCarriesAValue)
{
    appbox::NetworkModel model;
    appbox::ProxyConfig  proxy;
    proxy.server = L"proxy.example";
    SetProxy(model, proxy);

    const auto document = nlohmann::json::parse(BuildOrFail(model));

    /*
     * A proxy which is configured but carries no traffic is still part of the
     * document, so the workspace does not lose it while the user is editing.
     */
    ASSERT_TRUE(document.contains("proxy"));
    EXPECT_EQ(document["proxy"]["tcp"], false);
    EXPECT_EQ(document["proxy"]["udp"], false);
    EXPECT_EQ(document["proxy"]["server"], "proxy.example");
    EXPECT_EQ(document["proxy"]["port"], "");
}

TEST(Unit_NetworkIsolationFile, LeavesTheProxyOutWhileTheModelHoldsNone)
{
    appbox::NetworkModel model;
    AddEntry(model, L"example.com", L"127.0.0.1");

    const auto document = nlohmann::json::parse(BuildOrFail(model));

    EXPECT_FALSE(document.contains("proxy"));
}
