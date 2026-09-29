#include "utils/WinAPI.h" /* Must be first include file */
#include <gtest/gtest.h>
#include "environment/Configuration.hpp"
#include "environment/Table.hpp"
#include <string>
#include <vector>

namespace
{

/**
 * @brief Build an environment block from a list of entries.
 * @param[in] entries Entries of the block, each one `name=value`.
 * @return The block, terminated by a second null character.
 */
std::wstring MakeBlock(const std::vector<std::wstring>& entries)
{
    std::wstring block;
    for (const auto& entry : entries)
    {
        block.append(entry);
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

/**
 * @brief Build an ANSI environment block from a list of entries.
 * @param[in] entries Entries of the block, each one `name=value`.
 * @return The block, terminated by a second null character.
 */
std::string MakeAnsiBlock(const std::vector<std::string>& entries)
{
    std::string block;
    for (const auto& entry : entries)
    {
        block.append(entry);
        block.push_back('\0');
    }
    block.push_back('\0');
    return block;
}

/**
 * @brief Read the entries of an ANSI environment block.
 * @param[in] block The block to read.
 * @return The entries of the block.
 */
std::vector<std::string> ReadAnsiBlock(const char* block)
{
    std::vector<std::string> entries;
    const char*              cursor = block;
    while (*cursor != '\0')
    {
        const std::string entry(cursor);
        entries.push_back(entry);
        cursor += entry.size() + 1;
    }
    return entries;
}

/**
 * @brief Take the block of a table and release it.
 * @param[in,out] table The table to read.
 * @return The entries of the block.
 */
std::vector<std::wstring> BlockOf(appbox::environment::Table& table)
{
    wchar_t* block = table.CreateBlock();
    EXPECT_NE(block, nullptr);

    std::vector<std::wstring> entries;
    const wchar_t*            cursor = block;
    while (*cursor != L'\0')
    {
        const std::wstring entry(cursor);
        entries.push_back(entry);
        cursor += entry.size() + 1;
    }

    EXPECT_TRUE(table.ReleaseBlock(block));
    return entries;
}

/**
 * @brief The text of the isolation file of the cases.
 * @return The UTF-8 text of a document with two variables.
 */
std::string IsolationDocument()
{
    return "{\n"
           "  \"version\": 1,\n"
           "  \"entries\": [\n"
           "    { \"name\": \"PATH\", \"value\": \"C:/MyApp/bin\", \"isolation\": \"write_copy\",\n"
           "      \"merge\": \"prepend\", \"merge_string\": \";\" },\n"
           "    { \"name\": \"APPBOX_MODE\", \"value\": \"sandbox\", \"isolation\": \"full\",\n"
           "      \"merge\": \"replace\", \"merge_string\": \"\" }\n"
           "  ]\n"
           "}\n";
}

} // namespace

TEST(Unit_EnvironmentTable, ParsesAnEnvironmentBlock)
{
    appbox::environment::Table table;
    const std::wstring         block = MakeBlock({ L"A=1", L"B=2", L"EMPTY=" });

    table.AssignBlock(block.c_str());

    EXPECT_EQ(table.Count(), 3u);

    std::wstring value;
    EXPECT_TRUE(table.Get(L"A", value));
    EXPECT_EQ(value, L"1");
    EXPECT_TRUE(table.Get(L"B", value));
    EXPECT_EQ(value, L"2");
    EXPECT_TRUE(table.Get(L"EMPTY", value));
    EXPECT_EQ(value, L"");
    EXPECT_FALSE(table.Get(L"MISSING", value));
}

TEST(Unit_EnvironmentTable, KeepsTheDriveCurrentDirectoryOfTheHost)
{
    /*
     * The block of a process spells the drive relative current directory as
     * `=C:=C:\...`, which carries an empty name and has to survive a round trip.
     */
    appbox::environment::Table table;
    const std::wstring         block = MakeBlock({ L"=C:=C:\\work", L"PATH=foo" });

    table.AssignBlock(block.c_str());

    EXPECT_EQ(table.Count(), 2u);

    std::wstring value;
    EXPECT_TRUE(table.Get(L"=C:", value));
    EXPECT_EQ(value, L"C:\\work");
}

TEST(Unit_EnvironmentTable, NamesAreComparedIgnoringTheCase)
{
    appbox::environment::Table table;
    table.Set(L"Path", L"foo");

    EXPECT_TRUE(table.Contains(L"PATH"));
    EXPECT_TRUE(table.Contains(L"path"));

    std::wstring value;
    EXPECT_TRUE(table.Get(L"pAtH", value));
    EXPECT_EQ(value, L"foo");

    /* A variable which is stored again keeps its position. */
    table.Set(L"PATH", L"bar");
    EXPECT_EQ(table.Count(), 1u);
    EXPECT_TRUE(table.Get(L"Path", value));
    EXPECT_EQ(value, L"bar");
}

TEST(Unit_EnvironmentTable, SetKeepsTheOrderOfTheTable)
{
    appbox::environment::Table table;
    table.Set(L"A", L"1");
    table.Set(L"B", L"2");
    table.Set(L"C", L"3");
    table.Set(L"B", L"changed");

    const auto entries = table.Entries();
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].name, L"A");
    EXPECT_EQ(entries[1].name, L"B");
    EXPECT_EQ(entries[1].value, L"changed");
    EXPECT_EQ(entries[2].name, L"C");
}

TEST(Unit_EnvironmentTable, DeleteRemovesAVariable)
{
    appbox::environment::Table table;
    table.Set(L"A", L"1");
    table.Set(L"B", L"2");

    EXPECT_TRUE(table.Delete(L"b"));
    EXPECT_FALSE(table.Contains(L"B"));
    EXPECT_EQ(table.Count(), 1u);

    EXPECT_FALSE(table.Delete(L"B"));
    EXPECT_FALSE(table.Delete(L""));
}

TEST(Unit_EnvironmentTable, ClearDropsEveryVariable)
{
    appbox::environment::Table table;
    table.Set(L"A", L"1");

    table.Clear();

    EXPECT_TRUE(table.IsEmpty());
    EXPECT_EQ(table.Count(), 0u);
}

TEST(Unit_EnvironmentTable, BlockRoundTripsTheTable)
{
    appbox::environment::Table table;
    table.Set(L"PATH", L"foo;bar");
    table.Set(L"EMPTY", L"");

    const auto entries = BlockOf(table);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0], L"PATH=foo;bar");
    EXPECT_EQ(entries[1], L"EMPTY=");

    appbox::environment::Table restored;
    const std::wstring         block = MakeBlock(entries);
    restored.AssignBlock(block.c_str());

    EXPECT_EQ(restored.Count(), 2u);

    std::wstring value;
    EXPECT_TRUE(restored.Get(L"path", value));
    EXPECT_EQ(value, L"foo;bar");
}

TEST(Unit_EnvironmentTable, ReleaseBlockRefusesABlockOfAnotherSource)
{
    appbox::environment::Table table;

    EXPECT_FALSE(table.ReleaseBlock(nullptr));

    wchar_t other[4] = { L'A', L'=', L'1', L'\0' };
    EXPECT_FALSE(table.ReleaseBlock(other));

    /* A block which the table handed out is released exactly once. */
    wchar_t* block = table.CreateBlock();
    ASSERT_NE(block, nullptr);
    EXPECT_TRUE(table.ReleaseBlock(block));
    EXPECT_FALSE(table.ReleaseBlock(block));
}

TEST(Unit_EnvironmentTable, CreateOwnedBlockIsNotOwnedByTheTable)
{
    appbox::environment::Table table;
    table.Set(L"A", L"1");

    wchar_t* block = table.CreateOwnedBlock();
    ASSERT_NE(block, nullptr);
    EXPECT_STREQ(block, L"A=1");

    /* The caller owns the block, so the table never releases it. */
    EXPECT_FALSE(table.ReleaseBlock(block));
    ::HeapFree(::GetProcessHeap(), 0, block);
}

TEST(Unit_EnvironmentTable, CreateAnsiBlockReportsTheSameVariables)
{
    appbox::environment::Table table;
    table.Set(L"A", L"1");
    table.Set(L"B", L"2");

    char* block = table.CreateAnsiBlock();
    ASSERT_NE(block, nullptr);

    const auto entries = ReadAnsiBlock(block);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0], "A=1");
    EXPECT_EQ(entries[1], "B=2");

    EXPECT_TRUE(table.ReleaseAnsiBlock(block));
    EXPECT_FALSE(table.ReleaseAnsiBlock(block));
}

TEST(Unit_EnvironmentTable, ParsesTheIsolationDocument)
{
    std::vector<appbox::environment::ConfiguredVariable> variables;
    std::string                                          error;

    ASSERT_TRUE(appbox::environment::ParseIsolationDocument(IsolationDocument(), variables, error)) << error;
    ASSERT_EQ(variables.size(), 2u);

    EXPECT_EQ(variables[0].name, L"PATH");
    EXPECT_EQ(variables[0].value, L"C:/MyApp/bin");
    EXPECT_EQ(variables[0].isolation, appbox::EnvironmentIsolation::WriteCopy);
    EXPECT_EQ(variables[0].merge, appbox::EnvironmentMergeMode::Prepend);
    EXPECT_EQ(variables[0].merge_string, L";");

    EXPECT_EQ(variables[1].name, L"APPBOX_MODE");
    EXPECT_EQ(variables[1].isolation, appbox::EnvironmentIsolation::Full);
    EXPECT_EQ(variables[1].merge, appbox::EnvironmentMergeMode::Replace);
}

TEST(Unit_EnvironmentTable, RefusesABrokenIsolationDocument)
{
    std::vector<appbox::environment::ConfiguredVariable> variables;
    std::string                                          error;

    EXPECT_FALSE(appbox::environment::ParseIsolationDocument("not json", variables, error));
    EXPECT_FALSE(error.empty());

    EXPECT_FALSE(appbox::environment::ParseIsolationDocument("[]", variables, error));

    EXPECT_FALSE(appbox::environment::ParseIsolationDocument("{ \"entries\": [] }", variables, error));

    EXPECT_FALSE(appbox::environment::ParseIsolationDocument("{ \"version\": 2, \"entries\": [] }", variables, error));

    /* An unknown mode names the entry which carries it. */
    const std::string unknown_mode =
        "{ \"version\": 1, \"entries\": [ { \"name\": \"A\", \"value\": \"1\", \"isolation\": \"whiteout\","
        " \"merge\": \"replace\", \"merge_string\": \"\" } ] }";
    EXPECT_FALSE(appbox::environment::ParseIsolationDocument(unknown_mode, variables, error));
    EXPECT_NE(error.find("entries[0]"), std::string::npos);

    const std::string no_name =
        "{ \"version\": 1, \"entries\": [ { \"name\": \"\", \"value\": \"1\", \"isolation\": \"full\","
        " \"merge\": \"replace\", \"merge_string\": \"\" } ] }";
    EXPECT_FALSE(appbox::environment::ParseIsolationDocument(no_name, variables, error));

    const std::string equals_sign =
        "{ \"version\": 1, \"entries\": [ { \"name\": \"A=B\", \"value\": \"1\", \"isolation\": \"full\","
        " \"merge\": \"replace\", \"merge_string\": \"\" } ] }";
    EXPECT_FALSE(appbox::environment::ParseIsolationDocument(equals_sign, variables, error));

    const std::string duplicate =
        "{ \"version\": 1, \"entries\": ["
        " { \"name\": \"Path\", \"value\": \"1\", \"isolation\": \"full\", \"merge\": \"replace\","
        " \"merge_string\": \"\" },"
        " { \"name\": \"PATH\", \"value\": \"2\", \"isolation\": \"full\", \"merge\": \"replace\","
        " \"merge_string\": \"\" } ] }";
    EXPECT_FALSE(appbox::environment::ParseIsolationDocument(duplicate, variables, error));

    /* A document which is refused leaves no entry behind. */
    EXPECT_TRUE(variables.empty());
}

TEST(Unit_EnvironmentTable, StateKeepsOneModificationPerVariable)
{
    appbox::environment::State state;
    state.Record(L"PATH", L"first");
    state.Record(L"path", L"second");
    state.Record(L"OTHER", L"value");

    EXPECT_EQ(state.Count(), 2u);

    const auto entries = state.Entries();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].name, L"PATH");
    EXPECT_EQ(entries[0].value, L"second");
    EXPECT_FALSE(entries[0].deleted);
    EXPECT_EQ(entries[1].name, L"OTHER");
}

TEST(Unit_EnvironmentTable, StateRecordsARemoval)
{
    appbox::environment::State state;
    state.Record(L"A", L"1");
    state.RecordDeletion(L"a");

    ASSERT_EQ(state.Count(), 1u);
    EXPECT_TRUE(state.Entries()[0].deleted);
    EXPECT_EQ(state.Entries()[0].value, L"");

    /* A variable which is stored again is no longer removed. */
    state.Record(L"A", L"2");
    EXPECT_FALSE(state.Entries()[0].deleted);
    EXPECT_EQ(state.Entries()[0].value, L"2");
}

TEST(Unit_EnvironmentTable, StateDocumentRoundTrips)
{
    appbox::environment::State state;
    state.Record(L"PATH", L"foo");
    state.RecordDeletion(L"GONE");

    std::string text;
    std::string error;
    ASSERT_TRUE(state.Build(text, error)) << error;
    EXPECT_NE(text.find("\"version\": 1"), std::string::npos);

    appbox::environment::State restored;
    ASSERT_TRUE(restored.Parse(text, error)) << error;

    const auto entries = restored.Entries();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].name, L"PATH");
    EXPECT_EQ(entries[0].value, L"foo");
    EXPECT_FALSE(entries[0].deleted);
    EXPECT_EQ(entries[1].name, L"GONE");
    EXPECT_TRUE(entries[1].deleted);
}

TEST(Unit_EnvironmentTable, RefusesABrokenStateDocument)
{
    appbox::environment::State state;
    std::string                error;

    EXPECT_FALSE(state.Parse("not json", error));
    EXPECT_FALSE(error.empty());

    EXPECT_FALSE(state.Parse("{ \"version\": 1 }", error));

    EXPECT_FALSE(state.Parse("{ \"version\": 2, \"entries\": [] }", error));

    /* The flag which removes a variable is part of every entry. */
    EXPECT_FALSE(state.Parse("{ \"version\": 1, \"entries\": [ { \"name\": \"A\", \"value\": \"1\" } ] }", error));

    EXPECT_FALSE(state.Parse("{ \"version\": 1, \"entries\": [ { \"name\": \"\", \"value\": \"1\","
                             " \"deleted\": false } ] }",
                             error));

    EXPECT_EQ(state.Count(), 0u);
}
