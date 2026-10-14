#include <gtest/gtest.h>
#include "src/core/SnapshotDiff.hpp"
#include "utils/SnapshotFile.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{

/**
 * @brief Build an entry of the filesystem part of a phase.
 * @param[in] preset_id Identifier of the preset directory.
 * @param[in] path Path below the preset directory.
 * @param[in] size Size of the file.
 * @param[in] write_time Last write time of the file.
 * @return The entry.
 */
appbox::SnapshotFileEntry MakeFileEntry(const char* preset_id, const std::wstring& path, std::uint64_t size,
                                        std::int64_t write_time)
{
    appbox::SnapshotFileEntry entry;
    entry.preset_id = preset_id;
    entry.path = path;
    entry.size = size;
    entry.write_time = write_time;
    return entry;
}

/**
 * @brief Build a directory entry of the filesystem part of a phase.
 * @param[in] preset_id Identifier of the preset directory.
 * @param[in] path Path below the preset directory.
 * @return The entry.
 */
appbox::SnapshotFileEntry MakeDirectoryEntry(const char* preset_id, const std::wstring& path)
{
    appbox::SnapshotFileEntry entry;
    entry.preset_id = preset_id;
    entry.path = path;
    entry.directory = true;
    return entry;
}

/**
 * @brief Build a value of the registry part of a phase.
 * @param[in] path Path of the key which holds the value.
 * @param[in] name Name of the value.
 * @param[in] type The `REG_*` type code of the value.
 * @param[in] data Raw data of the value.
 * @return The entry.
 */
appbox::SnapshotRegistryValueEntry MakeValueEntry(const std::wstring& path, const std::wstring& name,
                                                  std::uint32_t type, std::vector<std::uint8_t> data)
{
    appbox::SnapshotRegistryValueEntry entry;
    entry.path = path;
    entry.name = name;
    entry.type = type;
    entry.data = std::move(data);
    return entry;
}

/**
 * @brief Build a variable of the environment part of a phase.
 * @param[in] name Name of the variable.
 * @param[in] value Value of the variable.
 * @return The entry.
 */
appbox::SnapshotEnvironmentEntry MakeVariable(const std::wstring& name, const std::wstring& value)
{
    appbox::SnapshotEnvironmentEntry entry;
    entry.name = name;
    entry.value = value;
    return entry;
}

/**
 * @brief Compare two states a case built.
 *
 * The two states are written into the two phases of one snapshot file, which
 * is the shape the comparison reads, and the file is removed again when the
 * call is over.
 *
 * @param[in] before The state before the change.
 * @param[in] after The state after the change.
 * @param[out] diff The difference of the two states.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool DiffOf(const appbox::test::SnapshotPhaseContent& before, const appbox::test::SnapshotPhaseContent& after,
            appbox::SnapshotDiff& diff, std::string& error)
{
    const appbox::test::TempSnapshotFile file(L"diff.db");
    if (!appbox::test::WriteSnapshotFile(file.Get(), before, after, error))
    {
        return false;
    }

    return appbox::DiffSnapshotPhases(file.Get(), diff, error);
}

/**
 * @brief Get one entry of a list of files a difference reports.
 * @param[in] files The list of the difference.
 * @param[in] path Path of the entry.
 * @return The entry, null when the list does not hold the path.
 */
const appbox::SnapshotDiffFile* FindDiffFile(const std::vector<appbox::SnapshotDiffFile>& files,
                                             const std::wstring&                          path)
{
    for (const auto& entry : files)
    {
        if (entry.path == path)
        {
            return &entry;
        }
    }

    return nullptr;
}

/**
 * @brief Get one value of a list of registry values a difference reports.
 * @param[in] values The list of the difference.
 * @param[in] name Name of the value.
 * @return The value, null when the list does not hold the name.
 */
const appbox::SnapshotDiffRegistryValue* FindDiffValue(const std::vector<appbox::SnapshotDiffRegistryValue>& values,
                                                       const std::wstring&                                   name)
{
    for (const auto& entry : values)
    {
        if (entry.name == name)
        {
            return &entry;
        }
    }

    return nullptr;
}

/**
 * @brief Get one variable of a list of variables a difference reports.
 * @param[in] variables The list of the difference.
 * @param[in] name Name of the variable.
 * @return The variable, null when the list does not hold the name.
 */
const appbox::SnapshotDiffEnvironment* FindDiffVariable(const std::vector<appbox::SnapshotDiffEnvironment>& variables,
                                                        const std::wstring&                                 name)
{
    for (const auto& entry : variables)
    {
        if (entry.name == name)
        {
            return &entry;
        }
    }

    return nullptr;
}

} // namespace

TEST(Unit_SnapshotDiff, ReportsNoChangeOfTwoEqualStates)
{
    appbox::test::SnapshotPhaseContent state;
    state.files.push_back(MakeFileEntry("program_files", L"Foo\\app.exe", 10, 100));
    state.registry_keys.push_back(L"HKEY_CURRENT_USER\\Software\\Foo");
    state.registry_values.push_back(MakeValueEntry(L"HKEY_CURRENT_USER\\Software\\Foo", L"Server", 1, { 1, 2 }));
    state.environment.push_back(MakeVariable(L"APPBOX_TEST", L"1"));

    appbox::SnapshotDiff diff;
    std::string          error;
    ASSERT_TRUE(DiffOf(state, state, diff, error)) << error;

    EXPECT_TRUE(diff.IsEmpty());
}

TEST(Unit_SnapshotDiff, ReportsTheAddedModifiedAndRemovedFiles)
{
    appbox::test::SnapshotPhaseContent before;
    before.files.push_back(MakeDirectoryEntry("program_files", L"Foo"));
    before.files.push_back(MakeFileEntry("program_files", L"Foo\\gone.exe", 10, 100));
    before.files.push_back(MakeFileEntry("program_files", L"Foo\\same.exe", 10, 100));
    before.files.push_back(MakeFileEntry("program_files", L"Foo\\resized.exe", 10, 100));
    before.files.push_back(MakeFileEntry("program_files", L"Foo\\touched.exe", 10, 100));

    appbox::test::SnapshotPhaseContent after = before;
    after.files[3].size = 11;
    after.files[4].write_time = 101;

    /* The entry which the newer state does not hold anymore. */
    after.files.erase(after.files.begin() + 1);

    after.files.push_back(MakeFileEntry("program_files", L"Foo\\new.exe", 20, 200));
    after.files.push_back(MakeDirectoryEntry("program_files", L"Foo\\bin"));

    appbox::SnapshotDiff diff;
    std::string          error;
    ASSERT_TRUE(DiffOf(before, after, diff, error)) << error;

    ASSERT_EQ(diff.added_files.size(), 2u);
    const auto* added_file = FindDiffFile(diff.added_files, L"Foo\\new.exe");
    ASSERT_NE(added_file, nullptr);
    EXPECT_FALSE(added_file->directory);
    const auto* added_directory = FindDiffFile(diff.added_files, L"Foo\\bin");
    ASSERT_NE(added_directory, nullptr);
    EXPECT_TRUE(added_directory->directory);

    ASSERT_EQ(diff.modified_files.size(), 2u);
    EXPECT_NE(FindDiffFile(diff.modified_files, L"Foo\\resized.exe"), nullptr);
    EXPECT_NE(FindDiffFile(diff.modified_files, L"Foo\\touched.exe"), nullptr);

    ASSERT_EQ(diff.removed_files.size(), 1u);
    EXPECT_EQ(diff.removed_files[0].path, L"Foo\\gone.exe");
    EXPECT_EQ(diff.removed_files[0].preset_id, "program_files");

    EXPECT_FALSE(diff.IsEmpty());
}

TEST(Unit_SnapshotDiff, ReportsADirectoryOnlyAsAddedOrRemoved)
{
    /*
     * A directory carries neither a size nor a write time of its own, so the
     * two entries describe the same folder.
     */
    appbox::test::SnapshotPhaseContent before;
    before.files.push_back(MakeDirectoryEntry("program_files", L"Foo"));

    appbox::test::SnapshotPhaseContent after;
    after.files.push_back(MakeDirectoryEntry("program_files", L"Foo"));
    after.files[0].size = 4096;
    after.files[0].write_time = 99;

    appbox::SnapshotDiff diff;
    std::string          error;
    ASSERT_TRUE(DiffOf(before, after, diff, error)) << error;

    EXPECT_TRUE(diff.IsEmpty());
}

TEST(Unit_SnapshotDiff, IgnoresTheCaseOfEveryName)
{
    appbox::test::SnapshotPhaseContent before;
    before.files.push_back(MakeFileEntry("program_files", L"Foo\\App.EXE", 10, 100));
    before.registry_keys.push_back(L"HKEY_CURRENT_USER\\Software\\Foo");
    before.registry_values.push_back(MakeValueEntry(L"HKEY_CURRENT_USER\\Software\\Foo", L"Server", 1, { 1 }));
    before.environment.push_back(MakeVariable(L"Path", L"a"));

    appbox::test::SnapshotPhaseContent after;
    after.files.push_back(MakeFileEntry("program_files", L"foo\\app.exe", 10, 100));
    after.registry_keys.push_back(L"hkey_current_user\\software\\foo");
    after.registry_values.push_back(MakeValueEntry(L"hkey_current_user\\software\\foo", L"SERVER", 1, { 1 }));
    after.environment.push_back(MakeVariable(L"PATH", L"a"));

    appbox::SnapshotDiff diff;
    std::string          error;
    ASSERT_TRUE(DiffOf(before, after, diff, error)) << error;

    EXPECT_TRUE(diff.IsEmpty());
}

TEST(Unit_SnapshotDiff, ReportsTheAddedAndRemovedRegistryKeys)
{
    appbox::test::SnapshotPhaseContent before;
    before.registry_keys = { L"HKEY_CURRENT_USER", L"HKEY_CURRENT_USER\\Software\\Gone" };

    appbox::test::SnapshotPhaseContent after;
    after.registry_keys = { L"HKEY_CURRENT_USER", L"HKEY_CURRENT_USER\\Software\\New" };

    appbox::SnapshotDiff diff;
    std::string          error;
    ASSERT_TRUE(DiffOf(before, after, diff, error)) << error;

    ASSERT_EQ(diff.added_registry_keys.size(), 1u);
    EXPECT_EQ(diff.added_registry_keys[0], L"HKEY_CURRENT_USER\\Software\\New");
    ASSERT_EQ(diff.removed_registry_keys.size(), 1u);
    EXPECT_EQ(diff.removed_registry_keys[0], L"HKEY_CURRENT_USER\\Software\\Gone");
}

TEST(Unit_SnapshotDiff, ReportsAModifiedRegistryValueByItsTypeAndItsData)
{
    appbox::test::SnapshotPhaseContent before;
    before.registry_values.push_back(MakeValueEntry(L"HKEY_CURRENT_USER\\Software\\Foo", L"Same", 1, { 1, 2 }));
    before.registry_values.push_back(MakeValueEntry(L"HKEY_CURRENT_USER\\Software\\Foo", L"Retyped", 1, { 1, 2 }));
    before.registry_values.push_back(MakeValueEntry(L"HKEY_CURRENT_USER\\Software\\Foo", L"Changed", 1, { 1, 2 }));
    before.registry_values.push_back(MakeValueEntry(L"HKEY_CURRENT_USER\\Software\\Foo", L"Gone", 1, { 9 }));

    appbox::test::SnapshotPhaseContent after = before;
    after.registry_values[1].type = 4;
    after.registry_values[2].data = { 1, 3 };
    after.registry_values.push_back(MakeValueEntry(L"HKEY_CURRENT_USER\\Software\\Foo", L"New", 3, { 0xAA }));
    after.registry_values.erase(after.registry_values.begin() + 3);

    appbox::SnapshotDiff diff;
    std::string          error;
    ASSERT_TRUE(DiffOf(before, after, diff, error)) << error;

    ASSERT_EQ(diff.added_registry_values.size(), 1u);
    EXPECT_EQ(diff.added_registry_values[0].name, L"New");
    EXPECT_EQ(diff.added_registry_values[0].type, 3u);
    EXPECT_EQ(diff.added_registry_values[0].data, (std::vector<std::uint8_t>{ 0xAA }));

    ASSERT_EQ(diff.modified_registry_values.size(), 2u);
    const auto* retyped = FindDiffValue(diff.modified_registry_values, L"Retyped");
    ASSERT_NE(retyped, nullptr);
    EXPECT_EQ(retyped->type, 4u);
    const auto* changed = FindDiffValue(diff.modified_registry_values, L"Changed");
    ASSERT_NE(changed, nullptr);
    EXPECT_EQ(changed->data, (std::vector<std::uint8_t>{ 1, 3 }));

    ASSERT_EQ(diff.removed_registry_values.size(), 1u);
    EXPECT_EQ(diff.removed_registry_values[0].name, L"Gone");
    EXPECT_EQ(diff.removed_registry_values[0].data, (std::vector<std::uint8_t>{ 9 }));
}

TEST(Unit_SnapshotDiff, ReportsTheAddedModifiedAndRemovedVariables)
{
    appbox::test::SnapshotPhaseContent before;
    before.environment.push_back(MakeVariable(L"Same", L"1"));
    before.environment.push_back(MakeVariable(L"Changed", L"1"));
    before.environment.push_back(MakeVariable(L"Gone", L"1"));

    appbox::test::SnapshotPhaseContent after;
    after.environment.push_back(MakeVariable(L"Same", L"1"));
    after.environment.push_back(MakeVariable(L"Changed", L"2"));
    after.environment.push_back(MakeVariable(L"New", L"3"));

    appbox::SnapshotDiff diff;
    std::string          error;
    ASSERT_TRUE(DiffOf(before, after, diff, error)) << error;

    ASSERT_EQ(diff.added_environment.size(), 1u);
    EXPECT_EQ(diff.added_environment[0].name, L"New");
    EXPECT_EQ(diff.added_environment[0].value, L"3");

    ASSERT_EQ(diff.modified_environment.size(), 1u);
    EXPECT_EQ(diff.modified_environment[0].name, L"Changed");
    EXPECT_EQ(diff.modified_environment[0].value, L"2");

    ASSERT_EQ(diff.removed_environment.size(), 1u);
    EXPECT_EQ(diff.removed_environment[0].name, L"Gone");
}

TEST(Unit_SnapshotDiff, DoesNotDependOnTheOrderOfTheEntries)
{
    appbox::test::SnapshotPhaseContent before;
    before.files.push_back(MakeFileEntry("program_files", L"a.exe", 1, 1));
    before.files.push_back(MakeFileEntry("program_files", L"b.exe", 1, 1));
    before.registry_keys = { L"HKEY_CURRENT_USER\\A", L"HKEY_CURRENT_USER\\B" };
    before.environment = { MakeVariable(L"A", L"1"), MakeVariable(L"B", L"2") };

    appbox::test::SnapshotPhaseContent after;
    after.files.push_back(MakeFileEntry("program_files", L"b.exe", 2, 2));
    after.files.push_back(MakeFileEntry("program_files", L"a.exe", 1, 1));
    after.registry_keys = { L"HKEY_CURRENT_USER\\B" };
    after.environment = { MakeVariable(L"B", L"3"), MakeVariable(L"A", L"1") };

    appbox::SnapshotDiff straight;
    std::string          error;
    ASSERT_TRUE(DiffOf(before, after, straight, error)) << error;

    std::reverse(before.files.begin(), before.files.end());
    std::reverse(before.registry_keys.begin(), before.registry_keys.end());
    std::reverse(before.environment.begin(), before.environment.end());
    std::reverse(after.files.begin(), after.files.end());

    appbox::SnapshotDiff reversed;
    ASSERT_TRUE(DiffOf(before, after, reversed, error)) << error;

    ASSERT_EQ(reversed.modified_files.size(), straight.modified_files.size());
    ASSERT_EQ(reversed.modified_files.size(), 1u);
    EXPECT_EQ(reversed.modified_files[0].path, L"b.exe");

    ASSERT_EQ(reversed.removed_registry_keys.size(), straight.removed_registry_keys.size());
    ASSERT_EQ(reversed.removed_registry_keys.size(), 1u);
    EXPECT_EQ(reversed.removed_registry_keys[0], L"HKEY_CURRENT_USER\\A");

    ASSERT_EQ(reversed.modified_environment.size(), straight.modified_environment.size());
    ASSERT_EQ(reversed.modified_environment.size(), 1u);
    EXPECT_EQ(reversed.modified_environment[0].name, L"B");
}

TEST(Unit_SnapshotDiff, RefusesAFileWhichDoesNotHoldBothStates)
{
    const appbox::test::TempSnapshotFile file(L"only-before.db");

    appbox::test::SnapshotPhaseContent before;
    before.files.push_back(MakeFileEntry("program_files", L"Foo\\app.exe", 1, 1));

    std::string error;
    ASSERT_TRUE(appbox::test::WriteSnapshotPhase(file.Get(), appbox::SnapshotPhase::Before, before, error)) << error;

    /*
     * A phase which was never written holds no entry at all, so a comparison
     * against it would report the whole machine as removed.
     */
    appbox::SnapshotDiff diff;
    EXPECT_FALSE(appbox::DiffSnapshotPhases(file.Get(), diff, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(diff.IsEmpty());
}
