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
