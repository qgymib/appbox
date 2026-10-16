#include <gtest/gtest.h>
#include "src/core/RealtimeCapture.hpp"
#include "src/core/PresetDirectory.hpp"
#include "src/core/SnapshotApply.hpp"
#include "src/core/SnapshotCapture.hpp"
#include "WString.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Generate a unique name fragment for the folders and keys of a case.
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
    return L"appbox-realtime-" + UniqueFragment();
}

/**
 * @brief Build one file event of a recording.
 * @param[in] kind The kind of the change.
 * @param[in] path The Win32 path of the file.
 * @param[in] detail The new path of a rename.
 * @param[in] time The time of the event.
 * @return The event.
 */
appbox::RealtimeCaptureEvent FileEvent(appbox::RealtimeCaptureEvent::Kind kind, const std::wstring& path,
                                       const std::wstring& detail = {}, std::int64_t time = 0)
{
    appbox::RealtimeCaptureEvent event;
    event.kind = kind;
    event.process = L"installer.exe";
    event.path = path;
    event.detail = detail;
    event.time = time;
    return event;
}

/**
 * @brief Build one registry value event of a recording.
 * @param[in] kind The kind of the change.
 * @param[in] path The view path of the key.
 * @param[in] name The name of the value.
 * @param[in] time The time of the event.
 * @return The event.
 */
appbox::RealtimeCaptureEvent ValueEvent(appbox::RealtimeCaptureEvent::Kind kind, const std::wstring& path,
                                        const std::wstring& name, std::int64_t time = 0)
{
    appbox::RealtimeCaptureEvent event;
    event.kind = kind;
    event.process = L"installer.exe";
    event.path = path;
    event.detail = name;
    event.time = time;
    return event;
}

/**
 * @brief Harvest events with empty noise tables and no environment before.
 * @param[in] events The events of the recording.
 * @param[out] diff The difference the events produced.
 * @param[out] report Report of the changes which belong to no preset
 *             directory, may be null.
 * @return true on success.
 */
bool Harvest(const std::vector<appbox::RealtimeCaptureEvent>& events, appbox::SnapshotDiff& diff,
             appbox::CaptureFillReport* report = nullptr)
{
    appbox::CaptureFillReport ignored;
    std::string               error;
    if (!appbox::HarvestRealtimeEvents(events, {}, appbox::RealtimeCaptureOptions{}, diff,
                                       report == nullptr ? ignored : *report, error))
    {
        ADD_FAILURE() << "the harvest failed: " << error;
        return false;
    }
    return true;
}

/**
 * @brief Whether the added files of a difference hold an entry.
 * @param[in] diff The difference to search.
 * @param[in] preset_id Identifier of the preset directory of the entry.
 * @param[in] path Path below the preset directory.
 * @param[in] directory Whether the entry is a folder.
 * @return true when the entry is part of the difference.
 */
bool HasAddedFile(const appbox::SnapshotDiff& diff, const std::string& preset_id, const std::wstring& path,
                  bool directory)
{
    for (const auto& entry : diff.added_files)
    {
        if (entry.preset_id == preset_id && entry.path == path && entry.directory == directory)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Whether the removed files of a difference hold an entry.
 * @param[in] diff The difference to search.
 * @param[in] preset_id Identifier of the preset directory of the entry.
 * @param[in] path Path below the preset directory.
 * @return true when the entry is part of the difference.
 */
bool HasRemovedFile(const appbox::SnapshotDiff& diff, const std::string& preset_id, const std::wstring& path)
{
    for (const auto& entry : diff.removed_files)
    {
        if (entry.preset_id == preset_id && entry.path == path)
        {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(Unit_RealtimeCapture, TranslatesNtRegistryPaths)
{
    const std::wstring sid = L"S-1-5-21-1-2-3-4";

    EXPECT_EQ(appbox::NtRegistryPathToViewPath(L"\\REGISTRY\\MACHINE\\SOFTWARE\\Vendor", sid),
              L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Vendor");
    EXPECT_EQ(appbox::NtRegistryPathToViewPath(L"\\registry\\machine\\SOFTWARE\\Vendor", sid),
              L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Vendor");
    EXPECT_EQ(appbox::NtRegistryPathToViewPath(L"\\REGISTRY\\USER\\S-1-5-21-1-2-3-4\\Software\\Vendor", sid),
              L"HKEY_CURRENT_USER\\Software\\Vendor");
    EXPECT_EQ(appbox::NtRegistryPathToViewPath(L"\\REGISTRY\\USER\\S-1-5-21-9-9-9\\Software\\Vendor", sid),
              L"HKEY_USERS\\S-1-5-21-9-9-9\\Software\\Vendor");
    EXPECT_EQ(appbox::NtRegistryPathToViewPath(L"\\REGISTRY\\USER\\S-1-5-21-9-9-9_Classes\\Vendor", sid),
              L"HKEY_USERS\\S-1-5-21-9-9-9_Classes\\Vendor");
    EXPECT_TRUE(
        appbox::NtRegistryPathToViewPath(L"\\REGISTRY\\A\\{12345678-0000-0000-0000-000000000000}", sid).empty());
    EXPECT_TRUE(appbox::NtRegistryPathToViewPath(L"\\REGISTRY\\DUMMY", sid).empty());
    EXPECT_TRUE(appbox::NtRegistryPathToViewPath(L"\\REGISTRY\\USER", sid).empty());
}

TEST(Unit_RealtimeCapture, TranslatesNtFilePaths)
{
    std::vector<std::pair<std::wstring, std::wstring>> map;
    map.emplace_back(L"\\Device\\HarddiskVolume3", L"C:");
    map.emplace_back(L"\\Device\\HarddiskVolume4", L"E:");

    EXPECT_EQ(appbox::NtFilePathToWin32Path(L"\\Device\\HarddiskVolume4\\workspace\\a.txt", map),
              L"E:\\workspace\\a.txt");
    EXPECT_EQ(appbox::NtFilePathToWin32Path(L"\\Device\\HarddiskVolume3\\Windows\\notepad.exe", map),
              L"C:\\Windows\\notepad.exe");
    EXPECT_TRUE(appbox::NtFilePathToWin32Path(L"\\Device\\HarddiskVolume99\\a.txt", map).empty());
    EXPECT_TRUE(appbox::NtFilePathToWin32Path(L"\\Device\\HarddiskVolume4", map).empty());

    /*
     * The name of a folder arrives with a trailing separator and a name which
     * was joined onto a folder with a doubled one: both name the entry the
     * canonical path names, and two spellings of one entry would describe it
     * twice.
     */
    EXPECT_EQ(appbox::NtFilePathToWin32Path(L"\\Device\\HarddiskVolume4\\workspace\\Install\\", map),
              L"E:\\workspace\\Install");
    EXPECT_EQ(appbox::NtFilePathToWin32Path(L"\\Device\\HarddiskVolume4\\workspace\\\\Install\\App.exe", map),
              L"E:\\workspace\\Install\\App.exe");
    EXPECT_EQ(appbox::NtFilePathToWin32Path(L"\\Device\\HarddiskVolume4\\", map), L"E:\\");

    /*
     * A volume which is mounted at a folder of another volume is reached by the
     * path of that folder, so the table of the capture holds the mount point of
     * such a volume instead of a drive letter.
     */
    map.emplace_back(L"\\Device\\HarddiskVolume7", L"C:\\Users");
    EXPECT_EQ(appbox::NtFilePathToWin32Path(L"\\Device\\HarddiskVolume7\\Vendor\\a.txt", map),
              L"C:\\Users\\Vendor\\a.txt");
}

TEST(Unit_RealtimeCapture, ExcludesTheNoiseOfTheOptions)
{
    appbox::RealtimeCaptureOptions options;
    options.excluded_processes = { L"MsMpEng.exe", L"svchost.exe" };
    options.excluded_file_paths = { L"C:\\Windows\\Temp" };
    options.excluded_registry_paths = { L"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer" };

    auto event = FileEvent(appbox::RealtimeCaptureEvent::Kind::FileCreated, L"C:\\Program Files\\Vendor\\app.exe");
    EXPECT_FALSE(appbox::RealtimeCaptureEventExcluded(event, options));

    event.process = L"MSMPENG.EXE";
    EXPECT_TRUE(appbox::RealtimeCaptureEventExcluded(event, options));

    event.process = L"installer.exe";
    event.path = L"C:\\Windows\\Temp\\extract\\setup.msi";
    EXPECT_TRUE(appbox::RealtimeCaptureEventExcluded(event, options));

    event.path = L"C:\\Windows\\Temporaries\\a.txt";
    EXPECT_FALSE(appbox::RealtimeCaptureEventExcluded(event, options));

    event.kind = appbox::RealtimeCaptureEvent::Kind::ValueSet;
    event.path = L"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders";
    event.detail = L"Desktop";
    EXPECT_TRUE(appbox::RealtimeCaptureEventExcluded(event, options));

    event.path = L"HKEY_CURRENT_USER\\Software\\Vendor";
    EXPECT_FALSE(appbox::RealtimeCaptureEventExcluded(event, options));
}

TEST(Unit_RealtimeCapture, DefaultOptionsExcludeTheTempFolderOfTheUser)
{
    const auto options = appbox::DefaultRealtimeCaptureOptions();

    wchar_t temp[MAX_PATH] = {};
    ASSERT_GT(::GetTempPathW(MAX_PATH, temp), 0u);
    std::wstring folder(temp);
    while (!folder.empty() && folder.back() == L'\\')
    {
        folder.pop_back();
    }

    bool found = false;
    for (const auto& entry : options.excluded_file_paths)
    {
        if (entry == folder)
        {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

/**
 * The shell of the machine is the process the user works with, so the files the
 * user creates with it are changes of the machine and not noise.
 *
 * Expected: the default noise tables leave the events of the shell in, while
 * the state the shell rewrites on its own is still left out: a capture which
 * named the shell as noise would lose the file the user just created.
 */
TEST(Unit_RealtimeCapture, DefaultOptionsLeaveTheChangesOfTheShellIn)
{
    const auto options = appbox::DefaultRealtimeCaptureOptions();
    const auto profile = appbox::test::GetKnownFolderPath(L"#USERPROFILE#", false);

    auto event = FileEvent(appbox::RealtimeCaptureEvent::Kind::FileCreated, profile + L"\\test.txt");
    event.process = L"explorer.exe";
    EXPECT_FALSE(appbox::RealtimeCaptureEventExcluded(event, options));

    /*
     * The folders of the state of the shell are left out whatever the process
     * which writes them is: the state is not a change an installation made.
     */
    const auto recent = appbox::test::GetKnownFolderPath(L"#AppData#", false) + L"\\Microsoft\\Windows\\Recent";
    event.path = recent + L"\\Vendor.lnk";
    EXPECT_TRUE(appbox::RealtimeCaptureEventExcluded(event, options));

    event.process = L"installer.exe";
    EXPECT_TRUE(appbox::RealtimeCaptureEventExcluded(event, options));

    event.kind = appbox::RealtimeCaptureEvent::Kind::ValueSet;
    event.path = L"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store\\Account";
    event.detail = L"Data";
    EXPECT_TRUE(appbox::RealtimeCaptureEventExcluded(event, options));

    event.path = L"HKEY_CURRENT_USER\\Software\\Vendor\\App";
    EXPECT_FALSE(appbox::RealtimeCaptureEventExcluded(event, options));
}

TEST(Unit_RealtimeCapture, ReportsTheFolderTheRecordingCreatedOnItsOwn)
{
    const appbox::test::RealFsFolder root(L"#USERPROFILE#", UniqueFolderName());
    ASSERT_TRUE(root.WriteFile(L"Install\\App.exe", "binary"));
    ASSERT_TRUE(root.WriteFile(L"Install\\Data\\config.ini", "settings"));

    std::vector<appbox::RealtimeCaptureEvent> events;
    events.push_back(FileEvent(appbox::RealtimeCaptureEvent::Kind::FileCreated, (root.Get() / L"Install").wstring()));

    appbox::SnapshotDiff diff;
    ASSERT_TRUE(Harvest(events, diff));

    /*
     * The difference carries the entries the recording reported and nothing the
     * host holds around them: the files below the folder were not reported, so
     * they are not part of the change the capture fills.
     */
    const auto name = root.Get().filename().wstring();
    ASSERT_EQ(diff.added_files.size(), 1u);
    EXPECT_TRUE(HasAddedFile(diff, "user_profile", name + L"\\Install", true));
    EXPECT_TRUE(diff.removed_files.empty());
    EXPECT_TRUE(diff.modified_files.empty());
}

TEST(Unit_RealtimeCapture, ReportsTheFolderAndTheFileItCarries)
{
    const appbox::test::RealFsFolder root(L"#USERPROFILE#", UniqueFolderName());
    ASSERT_TRUE(root.WriteFile(L"Install\\App.exe", "binary"));

    std::vector<appbox::RealtimeCaptureEvent> events;
    events.push_back(
        FileEvent(appbox::RealtimeCaptureEvent::Kind::FileCreated, (root.Get() / L"Install").wstring(), {}, 1));
    events.push_back(FileEvent(appbox::RealtimeCaptureEvent::Kind::FileCreated,
                               (root.Get() / L"Install" / L"App.exe").wstring(), {}, 2));

    appbox::SnapshotDiff diff;
    ASSERT_TRUE(Harvest(events, diff));

    const auto name = root.Get().filename().wstring();
    ASSERT_EQ(diff.added_files.size(), 2u);
    EXPECT_TRUE(HasAddedFile(diff, "user_profile", name + L"\\Install", true));
    EXPECT_TRUE(HasAddedFile(diff, "user_profile", name + L"\\Install\\App.exe", false));
}

TEST(Unit_RealtimeCapture, ReportsAChangeWhichBelongsToNoPresetDirectory)
{
    std::vector<appbox::RealtimeCaptureEvent> events;
    events.push_back(FileEvent(appbox::RealtimeCaptureEvent::Kind::FileCreated, L"Z:\\NoPreset\\App.exe"));

    appbox::SnapshotDiff      diff;
    appbox::CaptureFillReport report;
    ASSERT_TRUE(Harvest(events, diff, &report));

    /*
     * The recording observes the whole machine, so the changes it cannot
     * express are reported instead of being dropped: the user has to see which
     * part of the installation the capture could not carry.
     */
    EXPECT_TRUE(diff.added_files.empty());
    ASSERT_EQ(report.ignored.size(), 1u);
    EXPECT_EQ(report.total_ignored, 1u);
    EXPECT_NE(report.ignored[0].message.find(L"Z:\\NoPreset\\App.exe"), std::wstring::npos)
        << appbox::WideToUTF8(report.ignored[0].message);
    EXPECT_NE(report.ignored[0].message.find(L"no preset directory"), std::wstring::npos)
        << appbox::WideToUTF8(report.ignored[0].message);
}

TEST(Unit_RealtimeCapture, ReportsAFileWhichWasCreatedAndRemovedAgain)
{
    const appbox::test::RealFsFolder root(L"#USERPROFILE#", UniqueFolderName());
    ASSERT_TRUE(root.WriteFile(L"gone.txt", "x"));

    std::vector<appbox::RealtimeCaptureEvent> events;
    events.push_back(
        FileEvent(appbox::RealtimeCaptureEvent::Kind::FileCreated, (root.Get() / L"gone.txt").wstring(), {}, 1));
    events.push_back(
        FileEvent(appbox::RealtimeCaptureEvent::Kind::FileDeleted, (root.Get() / L"gone.txt").wstring(), {}, 2));

    appbox::SnapshotDiff diff;
    ASSERT_TRUE(Harvest(events, diff));

    EXPECT_TRUE(diff.added_files.empty());
    EXPECT_TRUE(diff.removed_files.empty());
    EXPECT_TRUE(diff.modified_files.empty());
}

TEST(Unit_RealtimeCapture, ReportsARenameAsRemovalAndAddition)
{
    const appbox::test::RealFsFolder root(L"#USERPROFILE#", UniqueFolderName());
    ASSERT_TRUE(root.WriteFile(L"old.txt", "x"));
    ASSERT_TRUE(root.WriteFile(L"new.txt", "x"));

    std::vector<appbox::RealtimeCaptureEvent> events;
    events.push_back(FileEvent(appbox::RealtimeCaptureEvent::Kind::FileRenamed, (root.Get() / L"old.txt").wstring(),
                               (root.Get() / L"new.txt").wstring()));

    appbox::SnapshotDiff diff;
    ASSERT_TRUE(Harvest(events, diff));

    const auto name = root.Get().filename().wstring();
    EXPECT_TRUE(HasRemovedFile(diff, "user_profile", name + L"\\old.txt"));
    EXPECT_TRUE(HasAddedFile(diff, "user_profile", name + L"\\new.txt", false));
}

TEST(Unit_RealtimeCapture, ReportsAFileWhichWasWrittenAsModified)
{
    const appbox::test::RealFsFolder root(L"#USERPROFILE#", UniqueFolderName());
    ASSERT_TRUE(root.WriteFile(L"touched.txt", "x"));

    std::vector<appbox::RealtimeCaptureEvent> events;
    events.push_back(
        FileEvent(appbox::RealtimeCaptureEvent::Kind::FileWritten, (root.Get() / L"touched.txt").wstring()));

    appbox::SnapshotDiff diff;
    ASSERT_TRUE(Harvest(events, diff));

    const auto name = root.Get().filename().wstring();
    ASSERT_EQ(diff.modified_files.size(), 1u);
    EXPECT_EQ(diff.modified_files.front().path, name + L"\\touched.txt");
    EXPECT_EQ(diff.modified_files.front().preset_id, "user_profile");
    EXPECT_TRUE(diff.added_files.empty());
}

TEST(Unit_RealtimeCapture, LeavesOutAlternateDataStreams)
{
    const appbox::test::RealFsFolder root(L"#USERPROFILE#", UniqueFolderName());
    ASSERT_TRUE(root.WriteFile(L"App.exe", "binary"));

    /*
     * The kernel reports the streams of a file as paths of their own
     * (`App.exe:Zone.Identifier`), which the view cannot name: the recording
     * leaves them out, so the difference stays one the workspaces can be
     * filled with.
     */
    const auto stream = (root.Get() / L"App.exe:Zone.Identifier").wstring();

    std::vector<appbox::RealtimeCaptureEvent> events;
    events.push_back(FileEvent(appbox::RealtimeCaptureEvent::Kind::FileCreated, stream, {}, 1));
    events.push_back(FileEvent(appbox::RealtimeCaptureEvent::Kind::FileWritten, stream, {}, 2));
    events.push_back(FileEvent(appbox::RealtimeCaptureEvent::Kind::FileDeleted, stream, {}, 3));

    appbox::SnapshotDiff diff;
    ASSERT_TRUE(Harvest(events, diff));

    EXPECT_TRUE(diff.added_files.empty());
    EXPECT_TRUE(diff.modified_files.empty());
    EXPECT_TRUE(diff.removed_files.empty());

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;
    std::string               error;
    EXPECT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;
}

TEST(Unit_RealtimeCapture, ReadsTheRegistryValuesTheRecordingWrote)
{
    const auto key_name = L"Software\\AppBoxRealtime-" + UniqueFragment();
    const auto path = L"HKEY_CURRENT_USER\\" + key_name;

    HKEY key = nullptr;
    ASSERT_EQ(::RegCreateKeyExW(HKEY_CURRENT_USER, key_name.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE,
                                nullptr, &key, nullptr),
              ERROR_SUCCESS);
    const std::wstring text = L"C:\\Program Files\\Vendor";
    ASSERT_EQ(::RegSetValueExW(key, L"InstallDir", 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                               static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t))),
              ERROR_SUCCESS);
    ::RegCloseKey(key);

    std::vector<appbox::RealtimeCaptureEvent> events;
    events.push_back(ValueEvent(appbox::RealtimeCaptureEvent::Kind::ValueSet, path, L"InstallDir"));

    appbox::SnapshotDiff diff;
    ASSERT_TRUE(Harvest(events, diff));

    ASSERT_EQ(diff.added_registry_values.size(), 1u);
    EXPECT_EQ(diff.added_registry_values.front().path, path);
    EXPECT_EQ(diff.added_registry_values.front().name, L"InstallDir");
    ASSERT_EQ(diff.added_registry_values.front().type, REG_SZ);
    ASSERT_EQ(diff.added_registry_values.front().data.size(), (text.size() + 1) * sizeof(wchar_t));

    ASSERT_EQ(diff.added_registry_keys.size(), 1u);
    EXPECT_EQ(diff.added_registry_keys.front(), path);
    EXPECT_TRUE(diff.removed_registry_values.empty());
    EXPECT_TRUE(diff.removed_registry_keys.empty());

    ::RegDeleteTreeW(HKEY_CURRENT_USER, key_name.c_str());
}

TEST(Unit_RealtimeCapture, ReportsTheRegistryEntriesTheRecordingDeleted)
{
    const auto path = L"HKEY_CURRENT_USER\\Software\\AppBoxRealtimeGone-" + UniqueFragment();

    std::vector<appbox::RealtimeCaptureEvent> events;
    events.push_back(ValueEvent(appbox::RealtimeCaptureEvent::Kind::ValueDeleted, path, L"InstallDir"));
    events.push_back(ValueEvent(appbox::RealtimeCaptureEvent::Kind::KeyDeleted, path, {}, 1));

    appbox::SnapshotDiff diff;
    ASSERT_TRUE(Harvest(events, diff));

    ASSERT_EQ(diff.removed_registry_values.size(), 1u);
    EXPECT_EQ(diff.removed_registry_values.front().path, path);
    EXPECT_EQ(diff.removed_registry_values.front().name, L"InstallDir");

    ASSERT_EQ(diff.removed_registry_keys.size(), 1u);
    EXPECT_EQ(diff.removed_registry_keys.front(), path);
    EXPECT_TRUE(diff.added_registry_values.empty());
    EXPECT_TRUE(diff.added_registry_keys.empty());
}

TEST(Unit_RealtimeCapture, KeepsTheLastChangeOfARegistryValue)
{
    const auto path = L"HKEY_CURRENT_USER\\Software\\AppBoxRealtimeTwice-" + UniqueFragment();

    HKEY key = nullptr;
    ASSERT_EQ(::RegCreateKeyExW(HKEY_CURRENT_USER, (path.substr(18)).c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                KEY_WRITE, nullptr, &key, nullptr),
              ERROR_SUCCESS);
    const DWORD number = 42;
    ASSERT_EQ(::RegSetValueExW(key, L"Count", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&number), sizeof(number)),
              ERROR_SUCCESS);
    ::RegCloseKey(key);

    std::vector<appbox::RealtimeCaptureEvent> events;
    events.push_back(ValueEvent(appbox::RealtimeCaptureEvent::Kind::ValueSet, path, L"Count", 1));
    events.push_back(ValueEvent(appbox::RealtimeCaptureEvent::Kind::ValueDeleted, path, L"Count", 2));

    appbox::SnapshotDiff diff;
    ASSERT_TRUE(Harvest(events, diff));

    EXPECT_TRUE(diff.added_registry_values.empty());
    EXPECT_TRUE(diff.added_registry_keys.empty());
    ASSERT_EQ(diff.removed_registry_values.size(), 1u);

    ::RegDeleteTreeW(HKEY_CURRENT_USER, (path.substr(18)).c_str());
}

TEST(Unit_RealtimeCapture, DiffsThePersistedEnvironmentOfTheRecording)
{
    const auto name = L"APPBOX_REALTIME_TEST_" + UniqueFragment();

    /* The recording began before the variable existed. */
    std::vector<appbox::SnapshotEnvironmentEntry> before;
    std::string                                   error;
    ASSERT_TRUE(appbox::ReadPersistedEnvironment(before, error));

    HKEY key = nullptr;
    ASSERT_EQ(::RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_WRITE, &key), ERROR_SUCCESS);
    const std::wstring value = L"appbox";
    ASSERT_EQ(::RegSetValueExW(key, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                               static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))),
              ERROR_SUCCESS);
    ::RegCloseKey(key);

    appbox::SnapshotDiff      diff;
    appbox::CaptureFillReport report;
    ASSERT_TRUE(appbox::HarvestRealtimeEvents({}, before, appbox::RealtimeCaptureOptions{}, diff, report, error));

    bool added = false;
    for (const auto& entry : diff.added_environment)
    {
        if (entry.name == name)
        {
            added = entry.value == value;
        }
    }
    EXPECT_TRUE(added);

    /* The variable is removed again, so the recording which began without it
     * does not report it anymore. */
    ASSERT_EQ(::RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_WRITE, &key), ERROR_SUCCESS);
    ASSERT_EQ(::RegDeleteValueW(key, name.c_str()), ERROR_SUCCESS);
    ::RegCloseKey(key);

    appbox::SnapshotDiff      removed;
    appbox::CaptureFillReport removed_report;
    ASSERT_TRUE(
        appbox::HarvestRealtimeEvents({}, before, appbox::RealtimeCaptureOptions{}, removed, removed_report, error));

    bool gone = false;
    for (const auto& entry : removed.added_environment)
    {
        if (entry.name == name)
        {
            gone = true;
        }
    }
    EXPECT_FALSE(gone);
}

/**
 * The keywords of the file provider are the ones the manifest of a machine
 * declares for the events of the provider, which is what the enable of the
 * capture is derived from.
 *
 * Expected: the union of the keywords of every event which was declared, because
 * the identifier of an event belongs to the version of the manifest of the
 * machine and a capture which asked for the keywords of the events it reads on
 * the version it was written against would read no event that version numbers
 * differently.
 */
TEST(Unit_RealtimeCapture, DerivesTheEnableOfTheFileProviderFromItsManifest)
{
    std::vector<appbox::ProviderEvent> declared;
    unsigned long long                 expected = 0;
    for (unsigned short id = 10; id < 40; ++id)
    {
        appbox::ProviderEvent event;
        event.id = id;
        event.level = 4;
        event.keyword = 1ull << (id % 32);
        expected |= event.keyword;
        declared.push_back(event);
    }

    const auto enable = appbox::FileProviderEnableFrom(declared);
    EXPECT_TRUE(enable.Complete());
    EXPECT_EQ(enable.keywords, expected);
}

/**
 * A machine whose manifest does not declare an event the capture reads cannot
 * report the change that event names.
 *
 * Expected: the event is reported as missing and the keywords of the events
 * which were declared are kept.
 */
TEST(Unit_RealtimeCapture, ReportsAnEventTheManifestDoesNotDeclare)
{
    const auto ids = appbox::RealtimeCaptureFileEventIds();
    ASSERT_GT(ids.size(), 1u);

    std::vector<appbox::ProviderEvent> declared;
    unsigned long long                 expected = 0;
    for (std::size_t index = 1; index < ids.size(); ++index)
    {
        appbox::ProviderEvent event;
        event.id = ids[index];
        event.level = 4;
        event.keyword = 1ull << index;
        expected |= event.keyword;
        declared.push_back(event);
    }

    const auto enable = appbox::FileProviderEnableFrom(declared);
    EXPECT_FALSE(enable.Complete());
    ASSERT_EQ(enable.missing.size(), 1u);
    EXPECT_EQ(enable.missing.front(), ids.front());
    EXPECT_EQ(enable.keywords, expected);
}
