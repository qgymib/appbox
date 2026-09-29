#include <gtest/gtest.h>
#include "src/core/EnvironmentIsolationFile.hpp"
#include "src/core/EnvironmentModel.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace
{

/**
 * @brief Add an environment variable and fail the test when the model refuses it.
 * @param[in,out] model Model to update.
 * @param[in] name Name of the variable.
 * @param[in] value Value of the variable.
 */
void AddEntry(appbox::EnvironmentModel& model, const std::wstring& name, const std::wstring& value)
{
    appbox::EnvironmentEntry entry;
    entry.name = name;
    entry.value = value;

    std::string error;
    ASSERT_TRUE(model.AddEntry(entry, error)) << error;
}

/**
 * @brief Build the isolation file of a model and fail the test on failure.
 * @param[in] model Model to describe.
 * @return The text of the isolation file.
 */
std::string BuildOrFail(const appbox::EnvironmentModel& model)
{
    std::string text;
    std::string error;
    EXPECT_TRUE(appbox::BuildEnvironmentIsolationFile(model, text, error)) << error;
    return text;
}

} // namespace

TEST(Unit_EnvironmentIsolationFile, WritesTheSchemaOfAnEmptyModel)
{
    appbox::EnvironmentModel model;

    const auto document = nlohmann::json::parse(BuildOrFail(model));
    EXPECT_EQ(document["version"].get<int>(), 1);
    EXPECT_TRUE(document["entries"].is_array());
    EXPECT_TRUE(document["entries"].empty());
}

TEST(Unit_EnvironmentIsolationFile, WritesEveryEntryInModelOrder)
{
    appbox::EnvironmentModel model;
    AddEntry(model, L"PATH", L"C:/MyApp/bin");
    AddEntry(model, L"APPBOX_MODE", L"sandbox");

    const auto document = nlohmann::json::parse(BuildOrFail(model));

    const auto& entries = document["entries"];
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0]["name"].get<std::string>(), "PATH");
    EXPECT_EQ(entries[0]["value"].get<std::string>(), "C:/MyApp/bin");
    EXPECT_EQ(entries[1]["name"].get<std::string>(), "APPBOX_MODE");
    EXPECT_EQ(entries[1]["value"].get<std::string>(), "sandbox");
}

TEST(Unit_EnvironmentIsolationFile, WritesEveryMemberOfAnEntry)
{
    appbox::EnvironmentModel model;

    appbox::EnvironmentEntry entry;
    entry.name = L"PATH";
    entry.value = L"C:/MyApp/bin";
    entry.isolation = appbox::EnvironmentIsolation::Full;
    entry.merge = appbox::EnvironmentMergeMode::Append;
    entry.merge_string = L"|";
    std::string error;
    ASSERT_TRUE(model.AddEntry(entry, error)) << error;

    const auto document = nlohmann::json::parse(BuildOrFail(model));

    const auto& item = document["entries"][0];
    EXPECT_EQ(item["isolation"].get<std::string>(), "full");
    EXPECT_EQ(item["merge"].get<std::string>(), "append");
    EXPECT_EQ(item["merge_string"].get<std::string>(), "|");
}

TEST(Unit_EnvironmentIsolationFile, WritesTheModeTheModelWasGiven)
{
    /*
     * The search path rule is a rule of the workspace: a mode the model holds
     * is written as it is, also for the search path variable.
     */
    appbox::EnvironmentModel model;

    appbox::EnvironmentEntry entry;
    entry.name = L"PATH";
    entry.value = L"C:/MyApp/bin";
    entry.merge = appbox::EnvironmentMergeMode::Replace;
    std::string error;
    ASSERT_TRUE(model.AddEntry(entry, error)) << error;

    const auto document = nlohmann::json::parse(BuildOrFail(model));
    EXPECT_EQ(document["entries"][0]["merge"].get<std::string>(), "replace");
    EXPECT_EQ(document["entries"][0]["merge_string"].get<std::string>(), "");
}

TEST(Unit_EnvironmentIsolationFile, WritesTheNameAndTheValueAsUTF8)
{
    appbox::EnvironmentModel model;
    AddEntry(model, L"APPBOX_Caf\u00e9", L"caf\u00e9");

    const auto document = nlohmann::json::parse(BuildOrFail(model));
    EXPECT_EQ(document["entries"][0]["name"], "APPBOX_Caf\xc3\xa9");
    EXPECT_EQ(document["entries"][0]["value"], "caf\xc3\xa9");
}

TEST(Unit_EnvironmentIsolationFile, KeepsTheTextStableForAGivenModel)
{
    appbox::EnvironmentModel model;
    AddEntry(model, L"PATH", L"C:/MyApp/bin");

    EXPECT_EQ(BuildOrFail(model), BuildOrFail(model));
}

TEST(Unit_EnvironmentIsolationFile, RejectsAnEntryTheModelWouldRefuse)
{
    appbox::EnvironmentModel model;

    appbox::EnvironmentEntry entry;
    entry.name = L"PATH=x";
    entry.value = L"C:/MyApp/bin";

    std::string error;
    EXPECT_FALSE(model.AddEntry(entry, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(model.IsEmpty());
}
