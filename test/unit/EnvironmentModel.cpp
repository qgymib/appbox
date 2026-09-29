#include <gtest/gtest.h>
#include "src/core/EnvironmentModel.hpp"
#include <string>
#include <vector>

namespace
{

/**
 * @brief Build an entry with a name and a value.
 * @param[in] name Name of the variable.
 * @param[in] value Value of the variable.
 * @return The entry.
 */
appbox::EnvironmentEntry MakeEntry(const std::wstring& name, const std::wstring& value)
{
    appbox::EnvironmentEntry entry;
    entry.name = name;
    entry.value = value;
    return entry;
}

/**
 * @brief Append an entry and fail the test when the model refuses it.
 * @param[in,out] model Model to update.
 * @param[in] entry Entry to append.
 */
void AddEntry(appbox::EnvironmentModel& model, const appbox::EnvironmentEntry& entry)
{
    std::string error;
    ASSERT_TRUE(model.AddEntry(entry, error)) << error;
}

/**
 * @brief Take a copy of the entries of a model.
 * @param[in] model Model to read.
 * @return The entries of the model.
 */
std::vector<appbox::EnvironmentEntry> Snapshot(const appbox::EnvironmentModel& model)
{
    return model.Entries();
}

/**
 * @brief Compare the entries of a model with a snapshot.
 *
 * Every field of an entry is compared, so a case which pins that a refused call
 * leaves the model untouched compares the whole content of it.
 *
 * @param[in] model Model to read.
 * @param[in] expected Entries the model is expected to hold.
 */
void ExpectEntries(const appbox::EnvironmentModel& model, const std::vector<appbox::EnvironmentEntry>& expected)
{
    const auto& entries = model.Entries();
    ASSERT_EQ(entries.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(entries[index].name, expected[index].name) << "entry " << index;
        EXPECT_EQ(entries[index].value, expected[index].value) << "entry " << index;
        EXPECT_EQ(entries[index].isolation, expected[index].isolation) << "entry " << index;
        EXPECT_EQ(entries[index].merge, expected[index].merge) << "entry " << index;
        EXPECT_EQ(entries[index].merge_string, expected[index].merge_string) << "entry " << index;
    }
}

} // namespace

TEST(Unit_EnvironmentModel, AddsEntriesInInsertionOrder)
{
    appbox::EnvironmentModel model;
    EXPECT_TRUE(model.IsEmpty());

    AddEntry(model, MakeEntry(L"TEMP", L"C:\\temp"));
    AddEntry(model, MakeEntry(L"APPBOX_MODE", L"sandbox"));
    AddEntry(model, MakeEntry(L"COUNT", L"3"));

    ASSERT_EQ(model.Entries().size(), 3u);
    EXPECT_EQ(model.Entries()[0].name, L"TEMP");
    EXPECT_EQ(model.Entries()[1].name, L"APPBOX_MODE");
    EXPECT_EQ(model.Entries()[2].name, L"COUNT");
    EXPECT_FALSE(model.IsEmpty());
}

TEST(Unit_EnvironmentModel, StoresTheDefaultModesOfANewEntry)
{
    appbox::EnvironmentModel model;
    AddEntry(model, MakeEntry(L"TEMP", L"C:\\temp"));

    /* A variable which is not the search path replaces the value of the host
     * and carries no merge string. */
    ASSERT_EQ(model.Entries().size(), 1u);
    EXPECT_EQ(model.Entries()[0].isolation, appbox::EnvironmentIsolation::WriteCopy);
    EXPECT_EQ(model.Entries()[0].merge, appbox::EnvironmentMergeMode::Replace);
    EXPECT_TRUE(model.Entries()[0].merge_string.empty());
}

TEST(Unit_EnvironmentModel, RejectsAnEmptyName)
{
    appbox::EnvironmentModel model;
    AddEntry(model, MakeEntry(L"TEMP", L"C:\\temp"));
    const auto before = Snapshot(model);

    appbox::EnvironmentEntry empty;
    empty.value = L"C:\\temp";

    std::string error;
    EXPECT_FALSE(model.AddEntry(empty, error));
    EXPECT_NE(error.find("must not be empty"), std::string::npos) << error;
    ExpectEntries(model, before);
}

TEST(Unit_EnvironmentModel, RejectsANameWhichCarriesAnEqualsSign)
{
    appbox::EnvironmentModel model;
    std::string              error;

    /* The equals sign separates the name of a variable from its value inside
     * the environment block of a process. */
    EXPECT_FALSE(model.AddEntry(MakeEntry(L"TEMP=X", L"value"), error));
    EXPECT_NE(error.find("equals sign"), std::string::npos) << error;
    EXPECT_TRUE(model.IsEmpty());
}

TEST(Unit_EnvironmentModel, RejectsANameWhichIsListedTwice)
{
    appbox::EnvironmentModel model;
    AddEntry(model, MakeEntry(L"TEMP", L"C:\\temp"));
    const auto before = Snapshot(model);

    /* The environment of a process ignores the case, so `temp` names the
     * variable `TEMP` lists. */
    std::string error;
    EXPECT_FALSE(model.AddEntry(MakeEntry(L"temp", L"D:\\temp"), error));
    EXPECT_NE(error.find("listed twice"), std::string::npos) << error;
    ExpectEntries(model, before);
}

TEST(Unit_EnvironmentModel, FindsAnEntryIgnoringTheCase)
{
    appbox::EnvironmentModel model;
    AddEntry(model, MakeEntry(L"PATH", L"C:\\bin"));
    AddEntry(model, MakeEntry(L"TEMP", L"C:\\temp"));

    EXPECT_EQ(model.IndexOfName(L"path"), 0);
    EXPECT_EQ(model.IndexOfName(L"TEMP"), 1);
    EXPECT_EQ(model.IndexOfName(L"MISSING"), -1);
}

TEST(Unit_EnvironmentModel, ReplacesAnEntryInPlace)
{
    appbox::EnvironmentModel model;
    AddEntry(model, MakeEntry(L"TEMP", L"C:\\temp"));
    AddEntry(model, MakeEntry(L"COUNT", L"3"));

    auto edited = MakeEntry(L"COUNT", L"4");
    edited.isolation = appbox::EnvironmentIsolation::Full;
    edited.merge = appbox::EnvironmentMergeMode::Host;

    std::string error;
    ASSERT_TRUE(model.SetEntry(1, edited, error)) << error;

    ASSERT_EQ(model.Entries().size(), 2u);
    EXPECT_EQ(model.Entries()[0].name, L"TEMP");
    EXPECT_EQ(model.Entries()[1].name, L"COUNT");
    EXPECT_EQ(model.Entries()[1].value, L"4");
    EXPECT_EQ(model.Entries()[1].isolation, appbox::EnvironmentIsolation::Full);
    EXPECT_EQ(model.Entries()[1].merge, appbox::EnvironmentMergeMode::Host);
}

TEST(Unit_EnvironmentModel, AllowsAnEntryToChangeTheCaseOfItsOwnName)
{
    appbox::EnvironmentModel model;
    AddEntry(model, MakeEntry(L"PATH", L"C:\\bin"));

    std::string error;
    EXPECT_TRUE(model.SetEntry(0, MakeEntry(L"Path", L"C:\\bin"), error)) << error;
    EXPECT_EQ(model.Entries()[0].name, L"Path");
}

TEST(Unit_EnvironmentModel, RejectsTheNameOfAnotherEntry)
{
    appbox::EnvironmentModel model;
    AddEntry(model, MakeEntry(L"TEMP", L"C:\\temp"));
    AddEntry(model, MakeEntry(L"COUNT", L"3"));
    const auto before = Snapshot(model);

    std::string error;
    EXPECT_FALSE(model.SetEntry(1, MakeEntry(L"temp", L"D:\\temp"), error));
    EXPECT_NE(error.find("listed twice"), std::string::npos) << error;
    ExpectEntries(model, before);
}

TEST(Unit_EnvironmentModel, RejectsAnIndexOutsideTheModel)
{
    appbox::EnvironmentModel model;
    AddEntry(model, MakeEntry(L"TEMP", L"C:\\temp"));
    const auto before = Snapshot(model);

    std::string error;
    EXPECT_FALSE(model.SetEntry(1, MakeEntry(L"COUNT", L"3"), error));
    EXPECT_NE(error.find("does not name an environment variable"), std::string::npos) << error;
    EXPECT_FALSE(model.RemoveEntry(1));
    ExpectEntries(model, before);
}

TEST(Unit_EnvironmentModel, RemovesAnEntryAndKeepsTheOrderOfTheRest)
{
    appbox::EnvironmentModel model;
    AddEntry(model, MakeEntry(L"ONE", L"1"));
    AddEntry(model, MakeEntry(L"TWO", L"2"));
    AddEntry(model, MakeEntry(L"THREE", L"3"));

    EXPECT_TRUE(model.RemoveEntry(1));

    ASSERT_EQ(model.Entries().size(), 2u);
    EXPECT_EQ(model.Entries()[0].name, L"ONE");
    EXPECT_EQ(model.Entries()[1].name, L"THREE");
}

TEST(Unit_EnvironmentModel, ResetDropsEveryEntry)
{
    appbox::EnvironmentModel model;
    AddEntry(model, MakeEntry(L"TEMP", L"C:\\temp"));

    model.Reset();

    EXPECT_TRUE(model.IsEmpty());
    EXPECT_TRUE(model.Entries().empty());
}

TEST(Unit_EnvironmentModel, StoresFreeTextValues)
{
    appbox::EnvironmentModel model;

    auto entry = MakeEntry(L"GREETING", L"hello world; again");
    entry.merge = appbox::EnvironmentMergeMode::Append;
    entry.merge_string = L" | ";
    AddEntry(model, entry);

    ASSERT_EQ(model.Entries().size(), 1u);
    EXPECT_EQ(model.Entries()[0].value, L"hello world; again");
    EXPECT_EQ(model.Entries()[0].merge_string, L" | ");
}

TEST(Unit_EnvironmentModel, FillsTheSearchPathVariableWithTheDefaults)
{
    appbox::EnvironmentEntry entry = MakeEntry(L"PATH", L"C:\\MyApp\\bin");

    appbox::ApplyPathVariableDefaults(entry);

    /* The paths of the packaged application are searched before the paths of
     * the host, joined with the separator of a search path. */
    EXPECT_EQ(entry.merge, appbox::EnvironmentMergeMode::Prepend);
    EXPECT_EQ(entry.merge_string, L";");
}

TEST(Unit_EnvironmentModel, FillsTheSearchPathVariableWhateverItsCaseIs)
{
    for (const std::wstring& name : { std::wstring(L"path"), std::wstring(L"Path"), std::wstring(L"pAtH") })
    {
        appbox::EnvironmentEntry entry = MakeEntry(name, L"C:\\MyApp\\bin");
        appbox::ApplyPathVariableDefaults(entry);

        EXPECT_EQ(entry.merge, appbox::EnvironmentMergeMode::Prepend) << name;
        EXPECT_EQ(entry.merge_string, L";") << name;
    }
}

TEST(Unit_EnvironmentModel, FillsNoOtherName)
{
    appbox::EnvironmentEntry entry = MakeEntry(L"PATHX", L"value");
    entry.merge = appbox::EnvironmentMergeMode::Append;
    entry.merge_string = L":";

    appbox::ApplyPathVariableDefaults(entry);

    EXPECT_EQ(entry.merge, appbox::EnvironmentMergeMode::Append);
    EXPECT_EQ(entry.merge_string, L":");
}

TEST(Unit_EnvironmentModel, StoresTheModeItIsGivenForTheSearchPath)
{
    appbox::EnvironmentModel model;

    /* The model stores what it is given, so a mode the user picked by hand is
     * part of the model and of a project file which is written from it. */
    auto entry = MakeEntry(L"PATH", L"C:\\MyApp\\bin");
    entry.merge = appbox::EnvironmentMergeMode::Append;
    entry.merge_string = L":";
    AddEntry(model, entry);

    ASSERT_EQ(model.Entries().size(), 1u);
    EXPECT_EQ(model.Entries()[0].merge, appbox::EnvironmentMergeMode::Append);
    EXPECT_EQ(model.Entries()[0].merge_string, L":");
}

TEST(Unit_EnvironmentModel, NamesTheSearchPathVariable)
{
    EXPECT_TRUE(appbox::environment_isolation::IsPathVariableName(L"PATH"));
    EXPECT_TRUE(appbox::environment_isolation::IsPathVariableName(L"path"));
    EXPECT_TRUE(appbox::environment_isolation::IsPathVariableName(L"Path"));
    EXPECT_FALSE(appbox::environment_isolation::IsPathVariableName(L"PAT"));
    EXPECT_FALSE(appbox::environment_isolation::IsPathVariableName(L"PATHX"));
    EXPECT_FALSE(appbox::environment_isolation::IsPathVariableName(L"PATHEXT"));
    EXPECT_FALSE(appbox::environment_isolation::IsPathVariableName(L""));
}

TEST(Unit_EnvironmentModel, DisplayNamesFollowTheEnumerations)
{
    const auto& isolations = appbox::EnvironmentIsolationNames();
    ASSERT_EQ(isolations.size(), 2u);
    EXPECT_EQ(isolations[static_cast<std::size_t>(appbox::EnvironmentIsolation::Full)], L"Full");
    EXPECT_EQ(isolations[static_cast<std::size_t>(appbox::EnvironmentIsolation::WriteCopy)], L"Write Copy");

    const auto& merges = appbox::EnvironmentMergeModeNames();
    ASSERT_EQ(merges.size(), 4u);
    EXPECT_EQ(merges[static_cast<std::size_t>(appbox::EnvironmentMergeMode::Replace)], L"Replace");
    EXPECT_EQ(merges[static_cast<std::size_t>(appbox::EnvironmentMergeMode::Host)], L"Host");
    EXPECT_EQ(merges[static_cast<std::size_t>(appbox::EnvironmentMergeMode::Prepend)], L"Prepend");
    EXPECT_EQ(merges[static_cast<std::size_t>(appbox::EnvironmentMergeMode::Append)], L"Append");

    EXPECT_EQ(appbox::EnvironmentIsolationName(appbox::EnvironmentIsolation::Full), L"Full");
    EXPECT_EQ(appbox::EnvironmentIsolationName(appbox::EnvironmentIsolation::WriteCopy), L"Write Copy");
    EXPECT_EQ(appbox::EnvironmentMergeModeName(appbox::EnvironmentMergeMode::Prepend), L"Prepend");
    EXPECT_EQ(appbox::EnvironmentMergeModeName(appbox::EnvironmentMergeMode::Append), L"Append");
}

TEST(Unit_EnvironmentModel, IsolationTokensRoundTrip)
{
    using namespace appbox::environment_isolation;

    EXPECT_STREQ(IsolationToken(appbox::EnvironmentIsolation::Full), "full");
    EXPECT_STREQ(IsolationToken(appbox::EnvironmentIsolation::WriteCopy), "write_copy");

    appbox::EnvironmentIsolation isolation = appbox::EnvironmentIsolation::Full;
    EXPECT_TRUE(ParseIsolationToken("write_copy", isolation));
    EXPECT_EQ(isolation, appbox::EnvironmentIsolation::WriteCopy);
    EXPECT_TRUE(ParseIsolationToken("Write Copy", isolation));
    EXPECT_EQ(isolation, appbox::EnvironmentIsolation::WriteCopy);
    EXPECT_TRUE(ParseIsolationToken("write-copy", isolation));
    EXPECT_EQ(isolation, appbox::EnvironmentIsolation::WriteCopy);
    EXPECT_TRUE(ParseIsolationToken("WRITECOPY", isolation));
    EXPECT_EQ(isolation, appbox::EnvironmentIsolation::WriteCopy);
    EXPECT_TRUE(ParseIsolationToken("FULL", isolation));
    EXPECT_EQ(isolation, appbox::EnvironmentIsolation::Full);

    EXPECT_FALSE(ParseIsolationToken("hide", isolation));
    EXPECT_FALSE(ParseIsolationToken("", isolation));
}

TEST(Unit_EnvironmentModel, MergeModeTokensRoundTrip)
{
    using namespace appbox::environment_isolation;

    EXPECT_STREQ(MergeModeToken(appbox::EnvironmentMergeMode::Replace), "replace");
    EXPECT_STREQ(MergeModeToken(appbox::EnvironmentMergeMode::Host), "host");
    EXPECT_STREQ(MergeModeToken(appbox::EnvironmentMergeMode::Prepend), "prepend");
    EXPECT_STREQ(MergeModeToken(appbox::EnvironmentMergeMode::Append), "append");

    appbox::EnvironmentMergeMode merge = appbox::EnvironmentMergeMode::Replace;
    EXPECT_TRUE(ParseMergeModeToken("Host", merge));
    EXPECT_EQ(merge, appbox::EnvironmentMergeMode::Host);
    EXPECT_TRUE(ParseMergeModeToken("PREPEND", merge));
    EXPECT_EQ(merge, appbox::EnvironmentMergeMode::Prepend);
    EXPECT_TRUE(ParseMergeModeToken("append", merge));
    EXPECT_EQ(merge, appbox::EnvironmentMergeMode::Append);
    EXPECT_TRUE(ParseMergeModeToken("replace", merge));
    EXPECT_EQ(merge, appbox::EnvironmentMergeMode::Replace);

    EXPECT_FALSE(ParseMergeModeToken("merge", merge));
    EXPECT_FALSE(ParseMergeModeToken("", merge));
}

/**
 * @brief Every mode of the two columns is explained by a description.
 *
 * The description is the text the workspace shows for the mode of a row and for
 * the modes the `IsolationMode` and `MergeMode` columns offer, so every mode has
 * to name itself.
 */
TEST(Unit_EnvironmentModel, EveryModeIsDescribedForATooltip)
{
    for (const auto isolation : { appbox::EnvironmentIsolation::Full, appbox::EnvironmentIsolation::WriteCopy })
    {
        const std::wstring description = appbox::EnvironmentIsolationDescription(isolation);

        EXPECT_NE(description.find(L"Isolation mode"), std::wstring::npos) << description;
        EXPECT_NE(description.find(appbox::EnvironmentIsolationName(isolation)), std::wstring::npos) << description;
    }

    for (const auto merge : { appbox::EnvironmentMergeMode::Replace, appbox::EnvironmentMergeMode::Host,
                              appbox::EnvironmentMergeMode::Prepend, appbox::EnvironmentMergeMode::Append })
    {
        const std::wstring description = appbox::EnvironmentMergeModeDescription(merge);

        EXPECT_NE(description.find(L"Merge mode"), std::wstring::npos) << description;
        EXPECT_NE(description.find(appbox::EnvironmentMergeModeName(merge)), std::wstring::npos) << description;
    }
}

/**
 * @brief The descriptions tell the two columns of the workspace apart.
 *
 * An isolation mode decides whether the value of the host is visible at all, a
 * merge mode decides how the two values are joined, and the examples of the
 * merge modes pin the result of the joining.
 */
TEST(Unit_EnvironmentModel, TheDescriptionsCarryTheRulesOfTheModes)
{
    EXPECT_NE(appbox::EnvironmentIsolationDescription(appbox::EnvironmentIsolation::Full)
                  .find(L"does not see the value of the host"),
              std::wstring::npos);
    EXPECT_NE(appbox::EnvironmentIsolationDescription(appbox::EnvironmentIsolation::WriteCopy).find(L"merge mode"),
              std::wstring::npos);

    EXPECT_NE(appbox::EnvironmentMergeModeDescription(appbox::EnvironmentMergeMode::Replace).find(L"report 'bar'"),
              std::wstring::npos);
    EXPECT_NE(appbox::EnvironmentMergeModeDescription(appbox::EnvironmentMergeMode::Prepend).find(L"'bar;foo'"),
              std::wstring::npos);
    EXPECT_NE(appbox::EnvironmentMergeModeDescription(appbox::EnvironmentMergeMode::Append).find(L"'foo;bar'"),
              std::wstring::npos);
    EXPECT_NE(appbox::EnvironmentMergeModeDescription(appbox::EnvironmentMergeMode::Host).find(L"ignored"),
              std::wstring::npos);
}
