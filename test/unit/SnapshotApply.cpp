#include <gtest/gtest.h>
#include "src/core/SnapshotApply.hpp"
#include "src/core/PresetDirectory.hpp"
#include "src/core/RegistryModel.hpp"
#include "WString.hpp"
#include "utils/RealFsFolder.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{

/**
 * @brief Generate a unique name fragment for the folders of a case.
 * @return The unique fragment.
 */
std::wstring UniqueFragment()
{
    static unsigned counter = 0;
    const auto      ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_wstring(ticks) + L"-" + std::to_wstring(++counter);
}

/**
 * @brief Build the name of a folder below the user profile.
 * @return The unique folder name.
 */
std::wstring UniqueFolderName()
{
    return L"appbox-snapshot-apply-" + UniqueFragment();
}

/**
 * @brief Build an entry of the filesystem part of a difference.
 * @param[in] preset_id Identifier of the preset directory.
 * @param[in] path Path below the preset directory.
 * @param[in] directory Whether the entry is a directory.
 * @return The entry.
 */
appbox::SnapshotDiffFile FileEntry(const char* preset_id, const std::wstring& path, bool directory = false)
{
    appbox::SnapshotDiffFile entry;
    entry.preset_id = preset_id;
    entry.path = path;
    entry.directory = directory;
    return entry;
}

/**
 * @brief Build a value of the registry part of a difference.
 * @param[in] path Path of the key which holds the value.
 * @param[in] name Name of the value.
 * @param[in] type The `REG_*` type code of the value.
 * @param[in] data Raw data of the value.
 * @return The entry.
 */
appbox::SnapshotDiffRegistryValue ValueEntry(const std::wstring& path, const std::wstring& name, std::uint32_t type,
                                             std::vector<std::uint8_t> data)
{
    appbox::SnapshotDiffRegistryValue entry;
    entry.path = path;
    entry.name = name;
    entry.type = type;
    entry.data = std::move(data);
    return entry;
}

/**
 * @brief Build a variable of the environment part of a difference.
 * @param[in] name Name of the variable.
 * @param[in] value Value of the variable.
 * @return The entry.
 */
appbox::SnapshotDiffEnvironment VariableEntry(const std::wstring& name, const std::wstring& value)
{
    appbox::SnapshotDiffEnvironment entry;
    entry.name = name;
    entry.value = value;
    return entry;
}

/**
 * @brief Get the real path of a preset directory.
 * @param[in] preset_id Identifier of the preset directory.
 * @return The path, empty when the preset directory is not offered.
 */
std::wstring PresetPath(const std::string& preset_id)
{
    appbox::PresetDirectory preset;
    if (!appbox::FindPresetDirectory(preset_id, preset))
    {
        return {};
    }

    return preset.real_path;
}

/**
 * @brief Get the value of a key of a registry model.
 * @param[in] model The registry model.
 * @param[in] path Path of the key.
 * @param[in] name Name of the value.
 * @return The value, null when it is not listed.
 */
const appbox::RegistryValueEntry* FindValue(const appbox::RegistryModel& model, const std::wstring& path,
                                            const std::wstring& name)
{
    const auto* key = model.FindKey(path);
    if (key == nullptr)
    {
        return nullptr;
    }

    for (const auto& value : key->values)
    {
        if (value.name == name)
        {
            return &value;
        }
    }

    return nullptr;
}

/**
 * @brief The environment key of the machine, which the registry part skips.
 */
constexpr const wchar_t* kMachineEnvironmentKey =
    L"HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";

} // namespace

TEST(Unit_SnapshotApply, ImportsTheFolderTheNewerSnapshotAdded)
{
    const auto                 name = UniqueFolderName();
    appbox::test::RealFsFolder folder(L"#USERPROFILE#", name);
    ASSERT_TRUE(folder.WriteFile(L"bin\\app.exe", "EXE"));

    appbox::SnapshotDiff diff;
    diff.added_files.push_back(FileEntry("user_profile", name, true));
    diff.added_files.push_back(FileEntry("user_profile", name + L"\\bin", true));
    diff.added_files.push_back(FileEntry("user_profile", name + L"\\bin\\app.exe"));

    appbox::PackModel           model;
    appbox::RegistryModel       registry;
    appbox::EnvironmentModel    environment;
    appbox::SnapshotApplyReport report;
    std::string                 error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, error)) << error;

    /*
     * The whole folder is imported once, whatever number of entries below it
     * the difference reports.
     */
    EXPECT_EQ(report.imported_folders, 1u);
    EXPECT_TRUE(report.skipped.empty());

    appbox::ImportedFolder imported;
    ASSERT_TRUE(model.GetImport("user_profile", name, imported));
    EXPECT_EQ(std::filesystem::path(imported.source_path).lexically_normal(), folder.Get().lexically_normal());
}

TEST(Unit_SnapshotApply, KeepsAFolderWhichIsPartOfTheArchiveAlready)
{
    const auto                 name = UniqueFolderName();
    appbox::test::RealFsFolder folder(L"#USERPROFILE#", name);
    ASSERT_TRUE(folder.WriteFile(L"app.exe", "EXE"));

    appbox::PackModel model;
    std::string       detail;
    ASSERT_TRUE(model.RestoreImportedFolder("user_profile", name, folder.Get().wstring(), detail)) << detail;

    appbox::SnapshotDiff diff;
    diff.added_files.push_back(FileEntry("user_profile", name, true));
    diff.added_files.push_back(FileEntry("user_profile", name + L"\\app.exe"));

    appbox::RegistryModel       registry;
    appbox::EnvironmentModel    environment;
    appbox::SnapshotApplyReport report;
    std::string                 error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, error)) << error;

    EXPECT_EQ(report.imported_folders, 0u);
    EXPECT_TRUE(report.skipped.empty());
}

TEST(Unit_SnapshotApply, ReportsAFolderWhichExistedBeforeTheCapture)
{
    appbox::SnapshotDiff diff;
    diff.added_files.push_back(FileEntry("program_files", L"Existing\\new.dll"));
    diff.added_files.push_back(FileEntry("program_files", L"Existing\\deeper\\new.dll"));

    appbox::PackModel           model;
    appbox::RegistryModel       registry;
    appbox::EnvironmentModel    environment;
    appbox::SnapshotApplyReport report;
    std::string                 error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, error)) << error;

    EXPECT_EQ(report.imported_folders, 0u);
    ASSERT_EQ(report.skipped.size(), 1u);
    EXPECT_NE(report.skipped[0].find(L"Existing"), std::wstring::npos) << appbox::WideToUTF8(report.skipped[0]);
    EXPECT_NE(report.skipped[0].find(L"before the capture"), std::wstring::npos)
        << appbox::WideToUTF8(report.skipped[0]);
    EXPECT_TRUE(model.IsEmpty());
}

TEST(Unit_SnapshotApply, ReportsAFileAddedDirectlyBelowThePresetDirectory)
{
    appbox::SnapshotDiff diff;
    diff.added_files.push_back(FileEntry("windows", L"loose.ini"));

    appbox::PackModel           model;
    appbox::RegistryModel       registry;
    appbox::EnvironmentModel    environment;
    appbox::SnapshotApplyReport report;
    std::string                 error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, error)) << error;

    ASSERT_EQ(report.skipped.size(), 1u);
    EXPECT_NE(report.skipped[0].find(L"loose.ini"), std::wstring::npos) << appbox::WideToUTF8(report.skipped[0]);
    EXPECT_NE(report.skipped[0].find(L"cannot be imported on its own"), std::wstring::npos)
        << appbox::WideToUTF8(report.skipped[0]);
}

TEST(Unit_SnapshotApply, HidesTheEntriesTheNewerSnapshotRemoved)
{
    appbox::SnapshotDiff diff;
    diff.removed_files.push_back(FileEntry("program_files", L"MyApp\\bin\\old.dll"));
    diff.removed_files.push_back(FileEntry("program_files", L"MyApp\\gone", true));

    appbox::PackModel           model;
    appbox::RegistryModel       registry;
    appbox::EnvironmentModel    environment;
    appbox::SnapshotApplyReport report;
    std::string                 error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, error)) << error;

    EXPECT_EQ(report.whiteouts, 2u);

    const auto whiteouts = model.AllWhiteouts();
    ASSERT_EQ(whiteouts.size(), 2u);
    EXPECT_EQ(whiteouts[0].preset_id, "program_files");
    EXPECT_EQ(whiteouts[0].target_dir, L"MyApp\\bin");
    EXPECT_EQ(whiteouts[0].name, L"old.dll");
    EXPECT_EQ(whiteouts[1].target_dir, L"MyApp");
    EXPECT_EQ(whiteouts[1].name, L"gone");
}

TEST(Unit_SnapshotApply, WritesTheRegistryKeysAndTheValues)
{
    const auto files = PresetPath("program_files");
    ASSERT_FALSE(files.empty());

    appbox::SnapshotDiff diff;
    diff.added_registry_keys.push_back(L"HKEY_CURRENT_USER\\Software\\Foo");
    diff.added_registry_keys.push_back(L"HKEY_CURRENT_USER\\Software\\Foo\\Bar");

    /* The value of the newer snapshot spells the path of a preset directory out. */
    diff.added_registry_values.push_back(ValueEntry(L"HKEY_CURRENT_USER\\Software\\Foo", L"Server", REG_SZ,
                                                    appbox::RegistryStringData(files + L"\\Foo\\Bar.exe")));
    diff.modified_registry_values.push_back(
        ValueEntry(L"HKEY_CURRENT_USER\\Software\\Foo", L"Count", REG_DWORD, appbox::RegistryDwordData(7)));

    appbox::PackModel           model;
    appbox::RegistryModel       registry;
    appbox::EnvironmentModel    environment;
    appbox::SnapshotApplyReport report;
    std::string                 error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, error)) << error;

    EXPECT_EQ(report.registry_keys, 2u);
    EXPECT_EQ(report.registry_values, 2u);
    EXPECT_TRUE(report.skipped.empty());

    const auto* server = FindValue(registry, L"HKEY_CURRENT_USER\\Software\\Foo", L"Server");
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(appbox::RegistryStringValue(server->data), L"%APPBOX:ProgramFiles%\\Foo\\Bar.exe");

    /* A value which is created now follows the key it is created in. */
    EXPECT_EQ(server->isolation, appbox::RegistryValueIsolation::Full);

    const auto* count = FindValue(registry, L"HKEY_CURRENT_USER\\Software\\Foo", L"Count");
    ASSERT_NE(count, nullptr);

    std::uint32_t number = 0;
    ASSERT_TRUE(appbox::RegistryDwordValue(count->data, number));
    EXPECT_EQ(number, 7u);
}

TEST(Unit_SnapshotApply, HidesARemovedRegistryKeyAndItsSubKeys)
{
    appbox::SnapshotDiff diff;
    diff.removed_registry_keys.push_back(L"HKEY_CURRENT_USER\\Software\\Foo");
    diff.removed_registry_keys.push_back(L"HKEY_CURRENT_USER\\Software\\Foo\\Bar");
    diff.removed_registry_keys.push_back(L"HKEY_CURRENT_USER\\Software\\Foo\\Bar\\Deep");

    /* The value below the removed key is hidden with the key. */
    diff.removed_registry_values.push_back(
        ValueEntry(L"HKEY_CURRENT_USER\\Software\\Foo\\Bar", L"Gone", REG_DWORD, appbox::RegistryDwordData(1)));

    appbox::PackModel           model;
    appbox::RegistryModel       registry;
    appbox::EnvironmentModel    environment;
    appbox::SnapshotApplyReport report;
    std::string                 error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, error)) << error;

    /* The shallowest key is hidden, which hides its whole subtree. */
    EXPECT_EQ(report.hidden_registry_entries, 1u);
    EXPECT_EQ(report.registry_keys, 0u);

    const auto* key = registry.FindKey(L"HKEY_CURRENT_USER\\Software\\Foo");
    ASSERT_NE(key, nullptr);
    EXPECT_EQ(key->isolation, appbox::RegistryKeyIsolation::Hide);
}

TEST(Unit_SnapshotApply, HidesARemovedRegistryValue)
{
    appbox::SnapshotDiff diff;
    diff.removed_registry_values.push_back(
        ValueEntry(L"HKEY_CURRENT_USER\\Software\\Foo", L"Gone", REG_SZ, appbox::RegistryStringData(L"before")));

    appbox::PackModel           model;
    appbox::RegistryModel       registry;
    appbox::EnvironmentModel    environment;
    appbox::SnapshotApplyReport report;
    std::string                 error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, error)) << error;

    EXPECT_EQ(report.hidden_registry_entries, 1u);

    /*
     * The value travels into the hive with the data of the older snapshot and
     * is hidden, so the sandbox reports it as missing.
     */
    const auto* value = FindValue(registry, L"HKEY_CURRENT_USER\\Software\\Foo", L"Gone");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->isolation, appbox::RegistryValueIsolation::Hide);
    EXPECT_EQ(appbox::RegistryStringValue(value->data), L"before");
}

TEST(Unit_SnapshotApply, SkipsTheEnvironmentKeysOfTheMachine)
{
    appbox::SnapshotDiff diff;
    diff.added_registry_keys.push_back(kMachineEnvironmentKey);
    diff.added_registry_values.push_back(
        ValueEntry(kMachineEnvironmentKey, L"APPBOX_TEST", REG_SZ, appbox::RegistryStringData(L"1")));
    diff.removed_registry_keys.push_back(L"HKEY_CURRENT_USER\\Environment");

    appbox::PackModel           model;
    appbox::RegistryModel       registry;
    appbox::EnvironmentModel    environment;
    appbox::SnapshotApplyReport report;
    std::string                 error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, error)) << error;

    /* The environment workspace expresses these changes, so the registry part stays empty. */
    EXPECT_EQ(report.registry_keys, 0u);
    EXPECT_EQ(report.registry_values, 0u);
    EXPECT_EQ(report.hidden_registry_entries, 0u);
    EXPECT_TRUE(report.skipped.empty());

    EXPECT_EQ(registry.FindKey(L"HKEY_CURRENT_USER\\Software\\Foo"), nullptr);
    EXPECT_EQ(FindValue(registry, kMachineEnvironmentKey, L"APPBOX_TEST"), nullptr);
}

TEST(Unit_SnapshotApply, WritesTheEnvironmentVariables)
{
    const auto profile = PresetPath("user_profile");
    ASSERT_FALSE(profile.empty());

    appbox::SnapshotDiff diff;
    diff.added_environment.push_back(VariableEntry(L"APPBOX_NEW", profile + L"\\Foo"));
    diff.modified_environment.push_back(VariableEntry(L"APPBOX_OLD", L"plain"));

    appbox::PackModel           model;
    appbox::RegistryModel       registry;
    appbox::EnvironmentModel    environment;
    appbox::SnapshotApplyReport report;
    std::string                 error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, error)) << error;

    EXPECT_EQ(report.environment_entries, 2u);
    EXPECT_TRUE(report.skipped.empty());

    const auto& entries = environment.Entries();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].name, L"APPBOX_NEW");
    EXPECT_EQ(entries[0].value, L"%APPBOX:USERPROFILE%\\Foo");
    EXPECT_EQ(entries[0].isolation, appbox::EnvironmentIsolation::WriteCopy);
    EXPECT_EQ(entries[0].merge, appbox::environment_isolation::kDefaultMergeMode);
    EXPECT_TRUE(entries[0].merge_string.empty());

    EXPECT_EQ(entries[1].name, L"APPBOX_OLD");
    EXPECT_EQ(entries[1].value, L"plain");

    /* A modified variable replaces the entry it already had. */
    appbox::SnapshotDiff second;
    second.modified_environment.push_back(VariableEntry(L"APPBOX_OLD", L"changed"));
    ASSERT_TRUE(appbox::ApplySnapshotDiff(second, model, registry, environment, report, error)) << error;
    ASSERT_EQ(environment.Entries().size(), 2u);
    EXPECT_EQ(environment.Entries()[1].value, L"changed");
}

TEST(Unit_SnapshotApply, ReportsARemovedEnvironmentVariable)
{
    appbox::SnapshotDiff diff;
    diff.removed_environment.push_back(VariableEntry(L"APPBOX_GONE", L"1"));

    appbox::PackModel           model;
    appbox::RegistryModel       registry;
    appbox::EnvironmentModel    environment;
    appbox::SnapshotApplyReport report;
    std::string                 error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, error)) << error;

    EXPECT_EQ(report.environment_entries, 0u);
    ASSERT_EQ(report.skipped.size(), 1u);
    EXPECT_NE(report.skipped[0].find(L"APPBOX_GONE"), std::wstring::npos) << appbox::WideToUTF8(report.skipped[0]);
    EXPECT_TRUE(environment.IsEmpty());
}

TEST(Unit_SnapshotApply, RefusesADifferenceWhichCannotBeInterpreted)
{
    appbox::SnapshotDiff empty_preset;
    empty_preset.added_files.push_back(FileEntry("", L"Foo"));

    appbox::SnapshotDiff leaving_path;
    leaving_path.removed_files.push_back(FileEntry("program_files", L"..\\Foo"));

    appbox::SnapshotDiff absolute_path;
    absolute_path.added_files.push_back(FileEntry("program_files", L"\\Foo"));

    appbox::PackModel        model;
    appbox::RegistryModel    registry;
    appbox::EnvironmentModel environment;

    for (const auto* diff : { &empty_preset, &leaving_path, &absolute_path })
    {
        appbox::SnapshotApplyReport report;
        std::string                 error;
        EXPECT_FALSE(appbox::ApplySnapshotDiff(*diff, model, registry, environment, report, error));
        EXPECT_FALSE(error.empty());
    }

    /* A refused difference leaves every model untouched. */
    EXPECT_TRUE(model.IsEmpty());
    EXPECT_TRUE(environment.IsEmpty());
}

TEST(Unit_SnapshotApply, SummarizesTheEntriesWhichWereLeftOut)
{
    appbox::SnapshotDiff diff;
    for (int index = 0; index < 25; ++index)
    {
        diff.added_files.push_back(FileEntry("program_files", L"Folder" + std::to_wstring(index) + L"\\new.dll"));
    }

    appbox::PackModel           model;
    appbox::RegistryModel       registry;
    appbox::EnvironmentModel    environment;
    appbox::SnapshotApplyReport report;
    std::string                 error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, error)) << error;

    /* The first messages are listed, the rest is summarized by the last one. */
    ASSERT_EQ(report.skipped.size(), 21u);
    EXPECT_NE(report.skipped.back().find(L"5 further entries"), std::wstring::npos)
        << appbox::WideToUTF8(report.skipped.back());
}
