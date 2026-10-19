#include <gtest/gtest.h>
#include "src/core/SnapshotApply.hpp"
#include "src/core/PresetDirectory.hpp"
#include "src/core/RegistryModel.hpp"
#include "WString.hpp"
#include "utils/RealFsFolder.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
 * @brief Whether a list of the report holds an entry which names a text.
 * @param[in] entries The entries to search.
 * @param[in] text Text the message of an entry has to hold.
 * @return true when one of the entries names the text.
 */
bool Mentions(const std::vector<appbox::CaptureFillNotice>& entries, const std::wstring& text)
{
    return std::any_of(entries.begin(), entries.end(), [&text](const appbox::CaptureFillNotice& entry) {
        return entry.message.find(text) != std::wstring::npos;
    });
}

/**
 * @brief Whether the model holds a folder of the view at a path.
 * @param[in] model The pack model.
 * @param[in] preset_id Identifier of the preset directory.
 * @param[in] target_dir Path of the folder relative to the preset directory.
 * @return true when the folder is part of the view.
 */
bool IsFolderOfTheView(const appbox::PackModel& model, const std::string& preset_id, const std::wstring& target_dir)
{
    return model.IsDirectoryOfView(preset_id, target_dir);
}

/**
 * @brief RAII helper which owns one file directly below a preset directory.
 *
 * The capture fills a file of the layer root of a preset directory as well, so
 * a case which covers it needs a real file of the host and not a folder which
 * holds one. The file is removed again when the helper goes out of scope.
 */
class RealFsFile
{
public:
    /**
     * @brief Create the file.
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] name Name of the file inside the layer root.
     */
    RealFsFile(const std::string& preset_id, const std::wstring& name)
    {
        const auto root = PresetPath(preset_id);
        if (root.empty())
        {
            return;
        }

        path_ = std::filesystem::path(root) / name;
        std::ofstream stream(path_, std::ios::binary);
        stream << "APPBOX";
    }

    ~RealFsFile()
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    RealFsFile(const RealFsFile&) = delete;
    RealFsFile& operator=(const RealFsFile&) = delete;
    RealFsFile(RealFsFile&&) = delete;
    RealFsFile& operator=(RealFsFile&&) = delete;

    /**
     * @brief Path of the file.
     * @return The path of the file.
     */
    const std::filesystem::path& Get() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

/**
 * @brief The environment key of the machine, which the registry part skips.
 */
constexpr const wchar_t* kMachineEnvironmentKey =
    L"HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";

} // namespace

/**
 * @brief A file which was added below a folder that existed before the capture
 *        is placed at its own path, and the folder becomes a folder of the view.
 */
TEST(Unit_SnapshotApply, PlacesAFileBelowAFolderWhichExistedBeforeTheCapture)
{
    const auto                 name = UniqueFolderName();
    appbox::test::RealFsFolder folder(L"#USERPROFILE#", name);
    ASSERT_TRUE(folder.WriteFile(L"new.dll", "content"));

    appbox::SnapshotDiff diff;
    diff.added_files.push_back(FileEntry("user_profile", name + L"\\new.dll"));

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    EXPECT_EQ(report.applied, 1u);
    EXPECT_TRUE(report.errors.empty());
    EXPECT_TRUE(report.ignored.empty());

    /* The folder is an entry of the view, the file is an entry of the archive. */
    EXPECT_TRUE(IsFolderOfTheView(model, "user_profile", name));
    EXPECT_FALSE(model.HasImportedFileAt("user_profile", name));

    const auto files = model.FilesOf("user_profile", name);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].file_name, L"new.dll");
    EXPECT_EQ(std::filesystem::path(files[0].source_path).lexically_normal(),
              folder.Get().lexically_normal() / L"new.dll");
}

/**
 * @brief The folders of the way to a file are created as folders of the view.
 */
TEST(Unit_SnapshotApply, CreatesTheFoldersOfTheWayToAFile)
{
    const auto                 name = UniqueFolderName();
    appbox::test::RealFsFolder folder(L"#USERPROFILE#", name);
    ASSERT_TRUE(folder.WriteFile(L"a\\b\\deep.dll", "content"));

    appbox::SnapshotDiff diff;
    diff.added_files.push_back(FileEntry("user_profile", name + L"\\a\\b\\deep.dll"));

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    EXPECT_EQ(report.applied, 1u);
    EXPECT_TRUE(IsFolderOfTheView(model, "user_profile", name));
    EXPECT_TRUE(IsFolderOfTheView(model, "user_profile", name + L"\\a"));
    EXPECT_TRUE(IsFolderOfTheView(model, "user_profile", name + L"\\a\\b"));

    const auto files = model.FilesOf("user_profile", name + L"\\a\\b");
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].file_name, L"deep.dll");
}

/**
 * @brief A file of the layer root of a preset directory is placed there.
 */
TEST(Unit_SnapshotApply, PlacesAFileInTheLayerRoot)
{
    const auto name = UniqueFolderName() + L".dll";
    RealFsFile file("user_profile", name);
    ASSERT_TRUE(std::filesystem::exists(file.Get()));

    appbox::SnapshotDiff diff;
    diff.added_files.push_back(FileEntry("user_profile", name));

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    EXPECT_EQ(report.applied, 1u);
    EXPECT_TRUE(report.errors.empty());

    const auto files = model.FilesOf("user_profile", L"");
    ASSERT_EQ(files.size(), 1u);
    EXPECT_TRUE(files[0].target_dir.empty());
    EXPECT_EQ(files[0].file_name, name);
    EXPECT_EQ(std::filesystem::path(files[0].source_path).lexically_normal(), file.Get().lexically_normal());
}

/**
 * @brief A folder which was added is an entry of the view and carries nothing.
 */
TEST(Unit_SnapshotApply, CreatesTheFolderTheNewerStateAdded)
{
    const auto name = UniqueFolderName();

    appbox::SnapshotDiff diff;
    diff.added_files.push_back(FileEntry("user_profile", name, true));
    diff.added_files.push_back(FileEntry("user_profile", name + L"\\empty", true));

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    EXPECT_EQ(report.applied, 2u);
    EXPECT_TRUE(IsFolderOfTheView(model, "user_profile", name));
    EXPECT_TRUE(IsFolderOfTheView(model, "user_profile", name + L"\\empty"));

    /* The folder has no host counterpart, so it carries no imported file. */
    EXPECT_TRUE(model.AllImportedFiles().empty());
}

/**
 * @brief An entry which the view carries already is not written twice.
 */
TEST(Unit_SnapshotApply, KeepsAnEntryTheImportedFolderCarries)
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

    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    /* The imported folder travels as the whole host folder, so it carries the file. */
    EXPECT_EQ(report.applied, 0u);
    EXPECT_TRUE(report.errors.empty());
    EXPECT_TRUE(report.ignored.empty());
    EXPECT_TRUE(model.AllImportedFiles().empty());
}

/**
 * @brief A file which is not on the host anymore cannot travel into the archive.
 */
TEST(Unit_SnapshotApply, ReportsAFileWhichIsNotOnTheHostAnymore)
{
    const auto name = UniqueFolderName();

    appbox::SnapshotDiff diff;
    diff.added_files.push_back(FileEntry("user_profile", name + L"\\gone.dll"));

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    EXPECT_EQ(report.applied, 0u);
    EXPECT_TRUE(report.ignored.empty());
    ASSERT_EQ(report.errors.size(), 1u);
    EXPECT_TRUE(Mentions(report.errors, name + L"\\gone.dll")) << appbox::WideToUTF8(report.errors[0].message);
    EXPECT_EQ(report.total_errors, 1u);
}

/**
 * @brief The entries the newer state removed are hidden with their markers.
 */
TEST(Unit_SnapshotApply, HidesTheEntriesTheNewerStateRemoved)
{
    appbox::SnapshotDiff diff;
    diff.removed_files.push_back(FileEntry("program_files", L"MyApp\\bin\\old.dll"));
    diff.removed_files.push_back(FileEntry("program_files", L"MyApp\\gone", true));

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    EXPECT_EQ(report.applied, 2u);

    /* The markers are written from the shallowest path on. */
    const auto whiteouts = model.AllWhiteouts();
    ASSERT_EQ(whiteouts.size(), 2u);
    EXPECT_EQ(whiteouts[0].preset_id, "program_files");
    EXPECT_EQ(whiteouts[0].target_dir, L"MyApp");
    EXPECT_EQ(whiteouts[0].name, L"gone");
    EXPECT_EQ(whiteouts[1].target_dir, L"MyApp\\bin");
    EXPECT_EQ(whiteouts[1].name, L"old.dll");
}

/**
 * @brief A folder which is gone carries the entries below it, so only the folder
 *        is marked.
 */
TEST(Unit_SnapshotApply, HidesAFolderAndLeavesItsContentOut)
{
    appbox::SnapshotDiff diff;
    diff.removed_files.push_back(FileEntry("program_files", L"MyApp\\gone", true));
    diff.removed_files.push_back(FileEntry("program_files", L"MyApp\\gone\\a.dll"));
    diff.removed_files.push_back(FileEntry("program_files", L"MyApp\\gone\\deep", true));
    diff.removed_files.push_back(FileEntry("program_files", L"MyApp\\gone\\deep\\b.dll"));
    diff.removed_files.push_back(FileEntry("program_files", L"MyApp\\kept.dll"));

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    /*
     * The marker of the folder hides everything below it, so the entries of the
     * folder are left out and the entry next to it keeps its own marker.
     */
    EXPECT_EQ(report.applied, 2u);

    const auto whiteouts = model.AllWhiteouts();
    ASSERT_EQ(whiteouts.size(), 2u);
    EXPECT_EQ(whiteouts[0].target_dir, L"MyApp");
    EXPECT_EQ(whiteouts[0].name, L"gone");
    EXPECT_EQ(whiteouts[1].target_dir, L"MyApp");
    EXPECT_EQ(whiteouts[1].name, L"kept.dll");
}

/**
 * @brief An entry which is hidden already is left where it is.
 */
TEST(Unit_SnapshotApply, KeepsAnEntryWhichIsHiddenAlready)
{
    appbox::PackModel model;
    std::string       detail;
    ASSERT_TRUE(model.AddWhiteout("program_files", L"MyApp", L"gone.dll", detail)) << detail;

    appbox::SnapshotDiff diff;
    diff.removed_files.push_back(FileEntry("program_files", L"MyApp\\gone.dll"));

    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    EXPECT_EQ(report.applied, 0u);
    EXPECT_TRUE(report.errors.empty());
    EXPECT_EQ(model.AllWhiteouts().size(), 1u);
}

/**
 * @brief The registry part writes the keys and the values of the difference.
 */
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

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    EXPECT_EQ(report.applied, 4u);
    EXPECT_TRUE(report.errors.empty());
    EXPECT_TRUE(report.ignored.empty());

    const auto* server = FindValue(registry, L"HKEY_CURRENT_USER\\Software\\Foo", L"Server");
    ASSERT_NE(server, nullptr);

    /*
     * The path of the 64 bit `Program Files` folder is written as the reference
     * of that folder: `%APPBOX:ProgramFiles%` would name `Program Files (x86)`
     * in a 32 bit process.
     */
    EXPECT_EQ(appbox::RegistryStringValue(server->data), L"%APPBOX:ProgramFilesX64%\\Foo\\Bar.exe");

    /* A value which is created now follows the key it is created in. */
    EXPECT_EQ(server->isolation, appbox::RegistryValueIsolation::Full);

    const auto* count = FindValue(registry, L"HKEY_CURRENT_USER\\Software\\Foo", L"Count");
    ASSERT_NE(count, nullptr);

    std::uint32_t number = 0;
    ASSERT_TRUE(appbox::RegistryDwordValue(count->data, number));
    EXPECT_EQ(number, 7u);
}

/**
 * @brief A key which is gone is hidden with everything below it.
 */
TEST(Unit_SnapshotApply, HidesARemovedRegistryKeyAndItsSubKeys)
{
    appbox::SnapshotDiff diff;
    diff.removed_registry_keys.push_back(L"HKEY_CURRENT_USER\\Software\\Foo");
    diff.removed_registry_keys.push_back(L"HKEY_CURRENT_USER\\Software\\Foo\\Bar");
    diff.removed_registry_keys.push_back(L"HKEY_CURRENT_USER\\Software\\Foo\\Bar\\Deep");

    /* The value below the removed key is hidden with the key. */
    diff.removed_registry_values.push_back(
        ValueEntry(L"HKEY_CURRENT_USER\\Software\\Foo\\Bar", L"Gone", REG_DWORD, appbox::RegistryDwordData(1)));

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    /* The shallowest key is hidden, which hides its whole subtree. */
    EXPECT_EQ(report.applied, 1u);

    const auto* key = registry.FindKey(L"HKEY_CURRENT_USER\\Software\\Foo");
    ASSERT_NE(key, nullptr);
    EXPECT_EQ(key->isolation, appbox::RegistryKeyIsolation::Hide);
}

/**
 * @brief A value which is gone is written with the data of the older state and
 *        hidden, so the sandbox reports it as missing.
 */
TEST(Unit_SnapshotApply, HidesARemovedRegistryValue)
{
    appbox::SnapshotDiff diff;
    diff.removed_registry_values.push_back(
        ValueEntry(L"HKEY_CURRENT_USER\\Software\\Foo", L"Gone", REG_SZ, appbox::RegistryStringData(L"before")));

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    EXPECT_EQ(report.applied, 1u);

    const auto* value = FindValue(registry, L"HKEY_CURRENT_USER\\Software\\Foo", L"Gone");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->isolation, appbox::RegistryValueIsolation::Hide);
    EXPECT_EQ(appbox::RegistryStringValue(value->data), L"before");
}

/**
 * @brief A value of a type the workspace cannot store is ignored.
 */
TEST(Unit_SnapshotApply, IgnoresARegistryValueOfATypeTheWorkspaceCannotStore)
{
    appbox::SnapshotDiff diff;
    /* The type code is not one of the types the workspace stores. */
    diff.added_registry_values.push_back(ValueEntry(L"HKEY_CURRENT_USER\\Software\\AppBoxTest", L"Broken", 5, {}));

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    EXPECT_EQ(report.applied, 0u);
    EXPECT_TRUE(report.errors.empty());
    ASSERT_EQ(report.ignored.size(), 1u);
    EXPECT_TRUE(Mentions(report.ignored, L"Broken")) << appbox::WideToUTF8(report.ignored[0].message);
    EXPECT_EQ(report.total_ignored, 1u);
    EXPECT_EQ(registry.FindKey(L"HKEY_CURRENT_USER\\Software\\AppBoxTest"), nullptr);
}

/**
 * @brief The environment keys of the machine belong to the environment
 *        workspace, so the registry part leaves them out.
 */
TEST(Unit_SnapshotApply, SkipsTheEnvironmentKeysOfTheMachine)
{
    appbox::SnapshotDiff diff;
    diff.added_registry_keys.push_back(kMachineEnvironmentKey);
    diff.added_registry_values.push_back(
        ValueEntry(kMachineEnvironmentKey, L"APPBOX_TEST", REG_SZ, appbox::RegistryStringData(L"1")));
    diff.removed_registry_keys.push_back(L"HKEY_CURRENT_USER\\Environment");

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    EXPECT_EQ(report.applied, 0u);
    EXPECT_TRUE(report.ignored.empty());
    EXPECT_TRUE(report.errors.empty());

    EXPECT_EQ(registry.FindKey(L"HKEY_CURRENT_USER\\Software\\Foo"), nullptr);
    EXPECT_EQ(FindValue(registry, kMachineEnvironmentKey, L"APPBOX_TEST"), nullptr);
}

/**
 * @brief The environment part writes the variables of the difference.
 */
TEST(Unit_SnapshotApply, WritesTheEnvironmentVariables)
{
    const auto profile = PresetPath("user_profile");
    ASSERT_FALSE(profile.empty());

    appbox::SnapshotDiff diff;
    diff.added_environment.push_back(VariableEntry(L"APPBOX_NEW", profile + L"\\Foo"));
    diff.modified_environment.push_back(VariableEntry(L"APPBOX_OLD", L"plain"));

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    EXPECT_EQ(report.applied, 2u);
    EXPECT_TRUE(report.errors.empty());

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
    ASSERT_TRUE(appbox::ApplySnapshotDiff(second, model, registry, environment, report, {}, error)) << error;
    ASSERT_EQ(environment.Entries().size(), 2u);
    EXPECT_EQ(environment.Entries()[1].value, L"changed");
}

/**
 * @brief A variable which was removed cannot be expressed by a row of the
 *        environment workspace.
 */
TEST(Unit_SnapshotApply, IgnoresARemovedEnvironmentVariable)
{
    appbox::SnapshotDiff diff;
    diff.removed_environment.push_back(VariableEntry(L"APPBOX_GONE", L"1"));

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    EXPECT_EQ(report.applied, 0u);
    EXPECT_TRUE(report.errors.empty());
    ASSERT_EQ(report.ignored.size(), 1u);
    EXPECT_TRUE(Mentions(report.ignored, L"APPBOX_GONE")) << appbox::WideToUTF8(report.ignored[0].message);
    EXPECT_TRUE(environment.IsEmpty());
}

/**
 * @brief A difference which cannot be interpreted is refused before anything is
 *        written.
 */
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
        appbox::CaptureFillReport report;
        std::string               error;
        EXPECT_FALSE(appbox::ApplySnapshotDiff(*diff, model, registry, environment, report, {}, error));
        EXPECT_FALSE(error.empty());
    }

    /* A refused difference leaves every model untouched. */
    EXPECT_TRUE(model.IsEmpty());
    EXPECT_TRUE(environment.IsEmpty());
}

/**
 * @brief The lists of the report are capped and the counts cover every entry.
 */
TEST(Unit_SnapshotApply, CapsTheListsAndCountsTheRest)
{
    const auto name = UniqueFolderName();

    appbox::SnapshotDiff diff;
    for (int index = 0; index < 25; ++index)
    {
        diff.added_files.push_back(
            FileEntry("user_profile", name + L"\\Folder" + std::to_wstring(index) + L"\\new.dll"));
    }

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;

    /* The files are not on the host, so every one of them is an error. */
    ASSERT_EQ(report.errors.size(), 20u);
    EXPECT_EQ(report.total_errors, 25u);
    EXPECT_EQ(report.applied, 0u);
}

/**
 * @brief The fill reports the entry it writes and how it ended.
 */
TEST(Unit_SnapshotApply, ReportsTheWorkOfTheFill)
{
    const auto                 name = UniqueFolderName();
    appbox::test::RealFsFolder folder(L"#USERPROFILE#", name);
    ASSERT_TRUE(folder.WriteFile(L"app.exe", "EXE"));

    appbox::SnapshotDiff diff;
    diff.added_files.push_back(FileEntry("user_profile", name + L"\\app.exe"));

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;

    std::vector<appbox::CaptureFillProgress> progress;
    const auto collect = [&progress](const appbox::CaptureFillProgress& current) { progress.push_back(current); };

    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, collect, error)) << error;

    ASSERT_EQ(progress.size(), 2u);
    EXPECT_EQ(progress[0].domain, appbox::CaptureFillDomain::Filesystem);
    EXPECT_EQ(progress[0].state, appbox::CaptureFillState::Working);
    EXPECT_EQ(progress[0].object, L"Current User Directory\\" + name + L"\\app.exe");
    EXPECT_FALSE(progress[0].action.empty());

    EXPECT_EQ(progress[1].state, appbox::CaptureFillState::Succeeded);
    EXPECT_TRUE(progress[1].message.empty());
}

/**
 * @brief A caller which stops the fill before it began reports the reason into
 *        the list of the errors.
 */
TEST(Unit_SnapshotApply, ReportsAnErrorWhichACallerFound)
{
    appbox::CaptureFillReport report;
    appbox::AddErrorNotice(report, appbox::CaptureFillDomain::Filesystem, L"the difference is not usable");
    appbox::AddIgnoredNotice(report, appbox::CaptureFillDomain::Filesystem, L"the change belongs to no preset");

    ASSERT_EQ(report.errors.size(), 1u);
    EXPECT_EQ(report.total_errors, 1u);
    EXPECT_EQ(report.errors[0].domain, appbox::CaptureFillDomain::Filesystem);
    EXPECT_EQ(report.errors[0].message, L"the difference is not usable");

    ASSERT_EQ(report.ignored.size(), 1u);
    EXPECT_EQ(report.total_ignored, 1u);
    EXPECT_EQ(report.ignored[0].message, L"the change belongs to no preset");
}
