#include "utils/CommonFixture.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "BuildCommandLine.hpp"
#include "src/core/PackModel.hpp"
#include "src/core/RealtimeCapture.hpp"
#include "src/core/RegistryModel.hpp"
#include "src/core/SnapshotApply.hpp"
#include "src/core/SnapshotCapture.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

typedef appbox::test::CommonFixture E2E_RealtimeCapture;
using namespace appbox::test;

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
 * @brief The noise tables a case records with.
 *
 * The default tables leave the process which runs the capture out, which is
 * the packer for a real run and the test here, so the entry of the test is
 * removed again to see the writes of the case itself.
 *
 * @return The options with the image of the test removed.
 */
appbox::RealtimeCaptureOptions WatchTheCaseOptions()
{
    auto options = appbox::DefaultRealtimeCaptureOptions();

    wchar_t self[MAX_PATH] = {};
    if (::GetModuleFileNameW(nullptr, self, MAX_PATH) == 0)
    {
        return options;
    }

    const auto image = std::filesystem::path(self).filename().wstring();
    options.excluded_processes.erase(std::remove_if(options.excluded_processes.begin(),
                                                    options.excluded_processes.end(),
                                                    [&image](const std::wstring& name) { return name == image; }),
                                     options.excluded_processes.end());
    return options;
}

/**
 * @brief Play one step of an installation: a folder with a file in it.
 *
 * The cases play the installation the capture observes on their own, so the
 * change they make is the one the events of the recording have to name.
 *
 * @param[in] root Folder which holds the change.
 * @param[in] name Name of the folder to create below the root.
 * @return The path of the folder which was created, empty on failure.
 */
std::wstring WriteAFolderWithAFile(const std::filesystem::path& root, const std::wstring& name)
{
    const auto folder = (root / name).wstring();
    if (::CreateDirectoryW(folder.c_str(), nullptr) == 0)
    {
        return {};
    }

    const auto   file = folder + L"\\App.exe";
    const HANDLE handle =
        ::CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return {};
    }

    const char content[] = "binary";
    DWORD      written = 0;
    const bool stored = ::WriteFile(handle, content, sizeof(content), &written, nullptr) != 0;
    ::CloseHandle(handle);
    return stored ? folder : std::wstring{};
}

/**
 * @brief Create a file which holds no byte.
 *
 * The shell creates a blank document by opening a file which does not exist
 * and closing it again without a byte in it, so a case which plays the user
 * creates the file the same way: `CREATE_ALWAYS` on a path which does not
 * exist is the create of an entry, and no write follows it.
 *
 * @param[in] path Path of the file to create.
 * @return true when the file was created.
 */
bool CreateABlankFile(const std::wstring& path)
{
    const HANDLE handle =
        ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    ::CloseHandle(handle);
    return true;
}

/**
 * @brief Create a blank file the way the shell does it.
 *
 * The noise tables of the capture name the images of the processes whose
 * events a recording leaves out, and the default table names the shell: the
 * image of a process is what the table compares, so the case copies the
 * command interpreter of the machine and gives the copy the name of the shell.
 * The command interpreter creates the file of a redirection the way the shell
 * creates a blank document, which makes the events of that process the events
 * of a process called `explorer.exe`.
 *
 * @param[in] cwd Working directory of the case, which holds the copy.
 * @param[in] path Path of the file to create.
 * @return true when the file was created.
 */
bool CreateABlankFileWithTheShellImage(const std::filesystem::path& cwd, const std::wstring& path)
{
    const auto image = cwd / L"explorer.exe";

    std::error_code ec;
    std::filesystem::copy_file(L"C:\\Windows\\System32\\cmd.exe", image,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        return false;
    }

    /*
     * The command is handed over as separate arguments, which the command
     * interpreter joins back into one line: the file of the redirection is
     * created by the process itself and not by a child of it.
     */
    const auto command = appbox::BuildCommandLine(image.wstring(), { L"/c", L"type", L"nul", L">", path });

    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');

    STARTUPINFOW startup_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);

    PROCESS_INFORMATION process_info;
    ZeroMemory(&process_info, sizeof(process_info));

    if (::CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                         &startup_info, &process_info) == 0)
    {
        return false;
    }

    ::CloseHandle(process_info.hThread);
    const auto waited = ::WaitForSingleObject(process_info.hProcess, 30000);

    DWORD      exit_code = 1;
    const bool reported = ::GetExitCodeProcess(process_info.hProcess, &exit_code) != 0;
    ::CloseHandle(process_info.hProcess);

    if (waited != WAIT_OBJECT_0 || !reported || exit_code != 0)
    {
        return false;
    }

    std::error_code exists_code;
    const bool      exists = std::filesystem::exists(std::filesystem::path(path), exists_code);
    return exists && !exists_code;
}

/**
 * @brief Removes the entries a case created outside the directory of the case.
 *
 * An entry a case creates below a folder of the host is removed by the helper
 * which owns that folder, while an entry of the root of a preset directory
 * belongs to no such folder: the guard removes the entries it was told about
 * when it goes out of scope, so a case which failed leaves the machine as it
 * found it as well.
 */
class RootEntryCleanup
{
public:
    ~RootEntryCleanup()
    {
        for (const auto& file : files_)
        {
            ::DeleteFileW(file.c_str());
        }
        for (const auto& folder : folders_)
        {
            ::RemoveDirectoryW(folder.c_str());
        }
    }

    RootEntryCleanup() = default;
    RootEntryCleanup(const RootEntryCleanup&) = delete;
    RootEntryCleanup& operator=(const RootEntryCleanup&) = delete;

    /**
     * @brief Watch a file of the root of a preset directory.
     * @param[in] path Path of the file.
     */
    void WatchFile(std::wstring path)
    {
        files_.push_back(std::move(path));
    }

    /**
     * @brief Watch a folder of the root of a preset directory.
     * @param[in] path Path of the folder.
     */
    void WatchFolder(std::wstring path)
    {
        folders_.push_back(std::move(path));
    }

private:
    std::vector<std::wstring> files_;
    std::vector<std::wstring> folders_;
};

/**
 * @brief Whether two texts are equal ignoring the case.
 * @param[in] left Left text.
 * @param[in] right Right text.
 * @return true when both texts differ in the case of their characters alone.
 */
bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (std::towlower(static_cast<std::wint_t>(left[index])) !=
            std::towlower(static_cast<std::wint_t>(right[index])))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether the events of the recording hold a change.
 *
 * The kernel reports the registry paths in the canonical spelling of their
 * key control blocks, which is upper case, so the comparison folds the case.
 *
 * @param[in] events The events of the recording.
 * @param[in] kind The kind of the change.
 * @param[in] path The path the change names.
 * @return true when one of the events matches.
 */
bool HasEvent(const std::vector<appbox::RealtimeCaptureEvent>& events, appbox::RealtimeCaptureEvent::Kind kind,
              const std::wstring& path)
{
    for (const auto& event : events)
    {
        if (event.kind == kind && EqualsIgnoreCase(event.path, path))
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Whether the events of the recording hold a rename of an entry.
 * @param[in] events The events of the recording.
 * @param[in] from The path the entry was renamed from.
 * @param[in] to The path the entry was renamed to.
 * @return true when one of the events names the rename.
 */
bool HasRename(const std::vector<appbox::RealtimeCaptureEvent>& events, const std::wstring& from,
               const std::wstring& to)
{
    for (const auto& event : events)
    {
        if (event.kind == appbox::RealtimeCaptureEvent::Kind::FileRenamed && EqualsIgnoreCase(event.path, from) &&
            EqualsIgnoreCase(event.detail, to))
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Whether the added files of a difference hold an entry.
 * @param[in] diff The difference to search.
 * @param[in] path Path below the preset directory of the entry.
 * @param[in] directory Whether the entry is a folder.
 * @return true when the entry is part of the difference.
 */
bool HasAddedFile(const appbox::SnapshotDiff& diff, const std::wstring& path, bool directory)
{
    for (const auto& entry : diff.added_files)
    {
        if (entry.preset_id == "user_profile" && entry.path == path && entry.directory == directory)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Whether the removed files of a difference hold an entry.
 * @param[in] diff The difference to search.
 * @param[in] path Path below the preset directory of the entry.
 * @return true when the entry is part of the difference.
 */
bool HasRemovedFile(const appbox::SnapshotDiff& diff, const std::wstring& path)
{
    for (const auto& entry : diff.removed_files)
    {
        if (entry.preset_id == "user_profile" && entry.path == path)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Whether the added files of a difference describe an entry twice.
 * @param[in] diff The difference to check.
 * @return true when one entry is described more than once.
 */
bool HasDuplicateAddedFile(const appbox::SnapshotDiff& diff)
{
    std::set<std::pair<std::string, std::wstring>> seen;
    for (const auto& entry : diff.added_files)
    {
        if (!seen.insert({ entry.preset_id, entry.path }).second)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Whether a key of a registry model holds a value.
 * @param[in] model The registry model.
 * @param[in] path Path of the key.
 * @param[in] name Name of the value.
 * @return true when the key holds the value.
 */
bool HasRegistryValue(const appbox::RegistryModel& model, const std::wstring& path, const std::wstring& name)
{
    const auto* key = model.FindKey(path);
    if (key == nullptr)
    {
        return false;
    }

    for (const auto& value : key->values)
    {
        if (value.name == name)
        {
            return true;
        }
    }
    return false;
}

} // namespace

/**
 * Condition:
 * 1. A realtime capture recording is started, and the case makes its change
 *    right away: it creates a registry key below the current user with a value
 *    in it and a folder below `#USERPROFILE#` with a file in it, which is the
 *    change an installation makes from the moment the capture runs.
 * 2. The recording is stopped and the events are taken.
 *
 * Expected:
 * 1. The events name every change of the case, so the recording carries the
 *    change which follows the start of the capture: the events of a provider
 *    are carried by the buffers of the session, and the consumer of the
 *    recording is the one which drains them.
 * 2. The session dropped no event, which is what the counters of the recording
 *    report: an event which was dropped is a change the capture never saw, and
 *    the counters are the only trace the loss leaves behind.
 */
TEST_F(E2E_RealtimeCapture, RecordsTheChangeWhichFollowsTheStart)
{
    if (!appbox::RealtimeCaptureSupported())
    {
        GTEST_SKIP() << "the machine does not carry the kernel events of the capture";
    }
    if (!appbox::IsProcessElevated())
    {
        GTEST_SKIP() << "the realtime capture needs an elevated process";
    }

    const appbox::test::RealFsFolder root(L"#USERPROFILE#", L"appbox-realtime-e2e-" + UniqueFragment());

    const auto         key_name = L"Software\\AppBoxRealtimeE2E-" + UniqueFragment();
    const auto         key_path = L"HKEY_CURRENT_USER\\" + key_name;
    const std::wstring text = L"C:\\Program Files\\Vendor";

    appbox::RealtimeCaptureRecorder recorder;
    const auto                      options = WatchTheCaseOptions();

    std::string error;
    if (!recorder.Start(options, appbox::RealtimeCaptureProgressCallback{}, error))
    {
        GTEST_SKIP() << "the recording could not be started: " << error;
    }

    HKEY key = nullptr;
    ASSERT_EQ(::RegCreateKeyExW(HKEY_CURRENT_USER, key_name.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE,
                                nullptr, &key, nullptr),
              ERROR_SUCCESS);
    ASSERT_EQ(::RegSetValueExW(key, L"InstallDir", 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                               static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t))),
              ERROR_SUCCESS);
    ::RegCloseKey(key);

    const auto folder = WriteAFolderWithAFile(root.Get(), L"Install");
    ASSERT_FALSE(folder.empty());
    const auto file = folder + L"\\App.exe";

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    recorder.RequestStop();
    ASSERT_TRUE(recorder.Join(15000));

    const auto statistics = recorder.Statistics();
    ASSERT_TRUE(statistics.available) << "the session did not report the counters of the recording";
    EXPECT_EQ(statistics.events_lost, 0u) << "the session dropped events before its consumer read them";

    const auto events = recorder.TakeEvents();
    ASSERT_FALSE(events.empty());
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::ValueSet, key_path));
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::FileCreated, folder));
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::FileCreated, file));

    ::RegDeleteTreeW(HKEY_CURRENT_USER, key_name.c_str());
}

/**
 * Condition:
 * 1. A realtime capture recording is started, which takes over the kernel
 *    logger of the machine, and it needs the elevation of the test process.
 * 2. While the recording runs, a folder below `#USERPROFILE#` is created with
 *    a file in it and a registry key below the current user is created with a
 *    value, which is the installation the capture observes.
 * 3. The recording is stopped, the events are taken and the analysis of the
 *    capture turns them into a snapshot difference.
 * 4. The entries of the case are taken apart from the rest of the difference,
 *    which also holds what the machine did meanwhile, and they are filled into
 *    the models of a session, which is what the last step of the wizard of the
 *    capture does.
 *
 * Expected:
 * 1. The events of the recording name the folder which was created, the file
 *    inside it, the write of that file and the registry value which was
 *    written: the write is tied to the file by the file object of the kernel,
 *    which the decoder reads by its name.
 * 2. The difference reports the folder with its file below the preset
 *    directory of the user profile, the registry value with the data the host
 *    holds, and no entry twice.
 * 3. The folder the recording created becomes a folder of the view and the file
 *    inside it is imported at its own path, so the entries of the recording
 *    travel without the rest of the folder.
 * 4. The registry value of the case is written into the workspace.
 */
TEST_F(E2E_RealtimeCapture, RecordsTheChangesOfARecording)
{
    if (!appbox::RealtimeCaptureSupported())
    {
        GTEST_SKIP() << "the machine does not carry the kernel events of the capture";
    }
    if (!appbox::IsProcessElevated())
    {
        GTEST_SKIP() << "the realtime capture needs an elevated process";
    }

    const appbox::test::RealFsFolder root(L"#USERPROFILE#", L"appbox-realtime-e2e-" + UniqueFragment());

    const auto         key_name = L"Software\\AppBoxRealtimeE2E-" + UniqueFragment();
    const auto         key_path = L"HKEY_CURRENT_USER\\" + key_name;
    const std::wstring text = L"C:\\Program Files\\Vendor";

    std::vector<appbox::SnapshotEnvironmentEntry> environment_before;
    std::string                                   error;
    ASSERT_TRUE(appbox::ReadPersistedEnvironment(environment_before, error));

    /* The recording: everything the process writes below the preset directory
     * of the user profile and into the registry is a change of the machine,
     * see WatchTheCaseOptions() for the noise the case leaves out. */
    appbox::RealtimeCaptureRecorder recorder;
    const auto                      options = WatchTheCaseOptions();

    if (!recorder.Start(options, appbox::RealtimeCaptureProgressCallback{}, error))
    {
        GTEST_SKIP() << "the recording could not be started: " << error;
    }

    const auto folder = WriteAFolderWithAFile(root.Get(), L"Install");
    ASSERT_FALSE(folder.empty());
    const auto file = folder + L"\\App.exe";

    HKEY key = nullptr;
    ASSERT_EQ(::RegCreateKeyExW(HKEY_CURRENT_USER, key_name.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE,
                                nullptr, &key, nullptr),
              ERROR_SUCCESS);
    ASSERT_EQ(::RegSetValueExW(key, L"InstallDir", 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                               static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t))),
              ERROR_SUCCESS);
    ::RegCloseKey(key);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    recorder.RequestStop();
    ASSERT_TRUE(recorder.Join(15000));

    const auto events = recorder.TakeEvents();
    ASSERT_FALSE(events.empty());

    const auto name = root.Get().filename().wstring();
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::FileCreated, folder));
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::FileCreated, file));
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::FileWritten, file));
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::ValueSet, key_path));

    /*
     * The analysis of the recording follows the semantics of the snapshot
     * capture, so the difference is filled with the same call: every entry the
     * recording reported is placed at its own path and nothing the host holds
     * around it travels with it.
     */
    appbox::SnapshotDiff      diff;
    appbox::CaptureFillReport report;
    ASSERT_TRUE(appbox::HarvestRealtimeEvents(events, environment_before, options, diff, report, error)) << error;

    EXPECT_TRUE(HasAddedFile(diff, name + L"\\Install", true));
    EXPECT_TRUE(HasAddedFile(diff, name + L"\\Install\\App.exe", false));

    /* The recording observes the whole machine, so the difference also holds
     * the changes of other programs: the case names its own value among them. */
    const appbox::SnapshotDiffRegistryValue* value_entry = nullptr;
    for (const auto& entry : diff.added_registry_values)
    {
        if (EqualsIgnoreCase(entry.path, key_path) && entry.name == L"InstallDir")
        {
            value_entry = &entry;
        }
    }
    ASSERT_NE(value_entry, nullptr);
    EXPECT_EQ(value_entry->type, REG_SZ);

    /*
     * The fill runs on the entries of the case alone: what the machine did
     * meanwhile would otherwise decide which entries the case checks, and a
     * capture of a whole machine holds far more of them than a case can name.
     */
    appbox::SnapshotDiff own;
    for (const auto& entry : diff.added_files)
    {
        if (entry.preset_id == "user_profile" && entry.path.compare(0, name.size(), name) == 0)
        {
            own.added_files.push_back(entry);
        }
    }
    for (const auto& entry : diff.added_registry_values)
    {
        if (EqualsIgnoreCase(entry.path, key_path))
        {
            own.added_registry_values.push_back(entry);
        }
    }

    /*
     * An entry must not be described twice, because the fill would write it
     * twice, and neither a stream of a file nor a path with the separators of
     * the caller may reach the difference.
     */
    EXPECT_FALSE(HasDuplicateAddedFile(diff));

    ASSERT_FALSE(own.added_files.empty());
    EXPECT_TRUE(HasAddedFile(own, name + L"\\Install", true));
    EXPECT_TRUE(HasAddedFile(own, name + L"\\Install\\App.exe", false));
    ASSERT_EQ(own.added_registry_values.size(), 1u);

    /*
     * The fill places the folder the recording created as a folder of the view
     * and imports the file the recording wrote inside it, and it writes the
     * registry value of the case: that is the whole work of the last step of
     * the wizard, which asks the user nothing.
     */
    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport own_report;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(own, model, registry, environment, own_report, {}, error)) << error;

    EXPECT_TRUE(own_report.errors.empty());
    EXPECT_TRUE(own_report.ignored.empty());
    EXPECT_EQ(own_report.applied, 3u);

    /*
     * The folder of the recording is an entry of the view and not an import of
     * the host folder, so the archive carries the file the recording wrote and
     * not the rest of the folder.
     */
    EXPECT_FALSE(model.HasImportedFileAt("user_profile", name + L"\\Install"));
    EXPECT_TRUE(model.HasImportedFileAt("user_profile", name + L"\\Install\\App.exe"));
    EXPECT_EQ(model.AllImportedFiles().size(), 1u);
    EXPECT_TRUE(HasRegistryValue(registry, own.added_registry_values.front().path, L"InstallDir"));

    ::RegDeleteTreeW(HKEY_CURRENT_USER, key_name.c_str());
}

/**
 * Condition:
 * 1. A realtime capture recording is started on the session of the capture and
 *    stopped right away without its events being taken, which is what the
 *    `Cancel` button of the wizard does.
 * 2. A second recording is started on the same recorder without the packer
 *    being closed, and the case plays one step of an installation while it
 *    runs.
 * 3. The second recording is stopped and its events are taken.
 *
 * Expected:
 * 1. The second recording starts, so a capture which follows a cancelled one
 *    works again: the recorder gives up the recording which ended before it
 *    takes the session of the machine for the new one.
 * 2. The events of the second recording name the folder of the case, so the
 *    new recording runs on a session of its own and is not the leftover of the
 *    cancelled one.
 */
TEST_F(E2E_RealtimeCapture, StartsANewRecordingAfterACancelledOne)
{
    if (!appbox::RealtimeCaptureSupported())
    {
        GTEST_SKIP() << "the machine does not carry the kernel events of the capture";
    }
    if (!appbox::IsProcessElevated())
    {
        GTEST_SKIP() << "the realtime capture needs an elevated process";
    }

    const appbox::test::RealFsFolder root(L"#USERPROFILE#", L"appbox-realtime-e2e-" + UniqueFragment());
    const auto                       options = WatchTheCaseOptions();

    appbox::RealtimeCaptureRecorder recorder;
    std::string                     error;
    ASSERT_TRUE(recorder.Start(options, appbox::RealtimeCaptureProgressCallback{}, error)) << error;

    /*
     * The recording is cancelled: the session is stopped and the events of the
     * recording are left behind, which is what the window does when the user
     * dismisses the wizard of the capture.
     */
    recorder.RequestStop();
    ASSERT_TRUE(recorder.Join(15000));

    ASSERT_TRUE(recorder.Start(options, appbox::RealtimeCaptureProgressCallback{}, error))
        << "the recording after a cancelled one did not start: " << error;

    const auto folder = WriteAFolderWithAFile(root.Get(), L"Install");
    ASSERT_FALSE(folder.empty());

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    recorder.RequestStop();
    ASSERT_TRUE(recorder.Join(15000));

    const auto events = recorder.TakeEvents();
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::FileCreated, folder));
}

/**
 * Condition:
 * 1. A realtime capture recording is started and ended the way a confirmed
 *    capture ends: the session is stopped, the consumer is joined and the
 *    events of the recording are taken.
 * 2. A second recording is started on the same recorder without the packer
 *    being closed, and the case plays one step of an installation while it
 *    runs.
 * 3. The second recording is stopped and its events are taken.
 *
 * Expected:
 * 1. The events of the first recording are the ones it produced, so they
 *    belong to the recording which was taken and not to the next one.
 * 2. The second recording starts, so a capture which follows a finished one
 *    works again.
 * 3. The events of the second recording name the folder of the case, and the
 *    events of the first recording are not handed out twice.
 */
TEST_F(E2E_RealtimeCapture, StartsANewRecordingAfterAFinishedOne)
{
    if (!appbox::RealtimeCaptureSupported())
    {
        GTEST_SKIP() << "the machine does not carry the kernel events of the capture";
    }
    if (!appbox::IsProcessElevated())
    {
        GTEST_SKIP() << "the realtime capture needs an elevated process";
    }

    const appbox::test::RealFsFolder root(L"#USERPROFILE#", L"appbox-realtime-e2e-" + UniqueFragment());
    const auto                       options = WatchTheCaseOptions();

    appbox::RealtimeCaptureRecorder recorder;
    std::string                     error;
    ASSERT_TRUE(recorder.Start(options, appbox::RealtimeCaptureProgressCallback{}, error)) << error;

    const auto first = WriteAFolderWithAFile(root.Get(), L"First");
    ASSERT_FALSE(first.empty());

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    recorder.RequestStop();
    ASSERT_TRUE(recorder.Join(15000));

    const auto first_events = recorder.TakeEvents();
    EXPECT_TRUE(HasEvent(first_events, appbox::RealtimeCaptureEvent::Kind::FileCreated, first));

    ASSERT_TRUE(recorder.Start(options, appbox::RealtimeCaptureProgressCallback{}, error))
        << "the recording after a finished one did not start: " << error;

    const auto second = WriteAFolderWithAFile(root.Get(), L"Second");
    ASSERT_FALSE(second.empty());

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    recorder.RequestStop();
    ASSERT_TRUE(recorder.Join(15000));

    const auto second_events = recorder.TakeEvents();
    EXPECT_TRUE(HasEvent(second_events, appbox::RealtimeCaptureEvent::Kind::FileCreated, second));
    EXPECT_FALSE(HasEvent(second_events, appbox::RealtimeCaptureEvent::Kind::FileCreated, first));
}

/**
 * Condition:
 * 1. A realtime capture recording is started on the session of the capture.
 * 2. A second recording is started on the same recorder while the first one
 *    still runs.
 * 3. The case plays one step of an installation while the first recording
 *    runs, which is stopped and taken afterwards.
 *
 * Expected:
 * 1. The second recording is refused with `a recording is already running`,
 *    because the session of the capture carries one recording alone.
 * 2. The refusal leaves the running recording alone: the events of the first
 *    recording name the folder the case created.
 */
TEST_F(E2E_RealtimeCapture, RefusesASecondRecordingWhileOneRuns)
{
    if (!appbox::RealtimeCaptureSupported())
    {
        GTEST_SKIP() << "the machine does not carry the kernel events of the capture";
    }
    if (!appbox::IsProcessElevated())
    {
        GTEST_SKIP() << "the realtime capture needs an elevated process";
    }

    const appbox::test::RealFsFolder root(L"#USERPROFILE#", L"appbox-realtime-e2e-" + UniqueFragment());
    const auto                       options = WatchTheCaseOptions();

    appbox::RealtimeCaptureRecorder recorder;
    std::string                     error;
    ASSERT_TRUE(recorder.Start(options, appbox::RealtimeCaptureProgressCallback{}, error)) << error;

    std::string refusal;
    EXPECT_FALSE(recorder.Start(options, appbox::RealtimeCaptureProgressCallback{}, refusal));
    EXPECT_EQ(refusal, "a recording is already running");

    const auto folder = WriteAFolderWithAFile(root.Get(), L"Install");
    ASSERT_FALSE(folder.empty());

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    recorder.RequestStop();
    ASSERT_TRUE(recorder.Join(15000));

    const auto events = recorder.TakeEvents();
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::FileCreated, folder));
}

/**
 * Condition:
 * 1. A folder below `#USERPROFILE#` holds two files before the recording
 *    starts.
 * 2. A realtime capture recording is started, and the case renames one file,
 *    deletes the other one, creates a registry key with a value in it and
 *    deletes the value and the key again.
 * 3. The recording is stopped, the events are taken and the analysis of the
 *    capture turns them into a snapshot difference.
 *
 * Expected:
 * 1. The events name the rename of the file with the path it was renamed to,
 *    the deletion of the other file, the write of the value and the deletion
 *    of the value and of its key.
 * 2. The difference reports the file which was renamed away and the file which
 *    was deleted as removed and the path the file was renamed to as added.
 * 3. The difference reports the value which was written and deleted as
 *    removed, together with the key it belonged to.
 */
TEST_F(E2E_RealtimeCapture, RecordsTheRenameAndTheDeletionOfAnEntry)
{
    if (!appbox::RealtimeCaptureSupported())
    {
        GTEST_SKIP() << "the machine does not carry the kernel events of the capture";
    }
    if (!appbox::IsProcessElevated())
    {
        GTEST_SKIP() << "the realtime capture needs an elevated process";
    }

    const appbox::test::RealFsFolder root(L"#USERPROFILE#", L"appbox-realtime-e2e-" + UniqueFragment());
    ASSERT_TRUE(root.WriteFile(L"old.txt", "content"));
    ASSERT_TRUE(root.WriteFile(L"gone.txt", "content"));

    const auto         key_name = L"Software\\AppBoxRealtimeE2E-" + UniqueFragment();
    const auto         key_path = L"HKEY_CURRENT_USER\\" + key_name;
    const std::wstring text = L"C:\\Program Files\\Vendor";

    std::vector<appbox::SnapshotEnvironmentEntry> environment_before;
    std::string                                   error;
    ASSERT_TRUE(appbox::ReadPersistedEnvironment(environment_before, error));

    appbox::RealtimeCaptureRecorder recorder;
    const auto                      options = WatchTheCaseOptions();
    if (!recorder.Start(options, appbox::RealtimeCaptureProgressCallback{}, error))
    {
        GTEST_SKIP() << "the recording could not be started: " << error;
    }

    const auto old_path = (root.Get() / L"old.txt").wstring();
    const auto new_path = (root.Get() / L"new.txt").wstring();
    const auto gone_path = (root.Get() / L"gone.txt").wstring();
    ASSERT_TRUE(::MoveFileW(old_path.c_str(), new_path.c_str()) != 0);
    ASSERT_TRUE(::DeleteFileW(gone_path.c_str()) != 0);

    /*
     * The key is created while the recording runs: the events of a registry
     * value name the block of its key, and the block of a key the kernel
     * already held is not reported again, which is what the capture needs to
     * turn the block into the path of the key.
     */
    HKEY key = nullptr;
    ASSERT_EQ(::RegCreateKeyExW(HKEY_CURRENT_USER, key_name.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE,
                                nullptr, &key, nullptr),
              ERROR_SUCCESS);
    ASSERT_EQ(::RegSetValueExW(key, L"InstallDir", 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                               static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t))),
              ERROR_SUCCESS);
    ASSERT_EQ(::RegDeleteValueW(key, L"InstallDir"), ERROR_SUCCESS);
    ::RegCloseKey(key);
    ASSERT_EQ(::RegDeleteKeyW(HKEY_CURRENT_USER, key_name.c_str()), ERROR_SUCCESS);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    recorder.RequestStop();
    ASSERT_TRUE(recorder.Join(15000));

    const auto events = recorder.TakeEvents();
    ASSERT_FALSE(events.empty());
    EXPECT_TRUE(HasRename(events, old_path, new_path));
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::FileDeleted, gone_path));
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::ValueSet, key_path));
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::ValueDeleted, key_path));
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::KeyDeleted, key_path));

    appbox::SnapshotDiff      diff;
    appbox::CaptureFillReport report;
    ASSERT_TRUE(appbox::HarvestRealtimeEvents(events, environment_before, options, diff, report, error)) << error;

    const auto name = root.Get().filename().wstring();
    EXPECT_TRUE(HasRemovedFile(diff, name + L"\\old.txt"));
    EXPECT_TRUE(HasRemovedFile(diff, name + L"\\gone.txt"));
    EXPECT_TRUE(HasAddedFile(diff, name + L"\\new.txt", false));

    bool value_removed = false;
    for (const auto& entry : diff.removed_registry_values)
    {
        if (EqualsIgnoreCase(entry.path, key_path) && entry.name == L"InstallDir")
        {
            value_removed = true;
        }
    }
    EXPECT_TRUE(value_removed);

    bool key_removed = false;
    for (const auto& entry : diff.removed_registry_keys)
    {
        if (EqualsIgnoreCase(entry, key_path))
        {
            key_removed = true;
        }
    }
    EXPECT_TRUE(key_removed);
}

/**
 * Condition:
 * 1. A realtime capture recording is started.
 * 2. The case plays what the user does while a capture runs: it creates three
 *    entries in the root of `#USERPROFILE#`, which is the preset directory the
 *    workspace shows as `Current User Directory` — a blank document made by a
 *    process which carries the name of the shell (see
 *    CreateABlankFileWithTheShellImage()), a blank document made by the case
 *    itself and a folder which holds nothing.
 * 3. The recording is stopped, the events are taken, the analysis turns them
 *    into a snapshot difference and the difference is filled into the models of
 *    a session.
 *
 * Expected:
 * 1. The events of the recording name every entry of the case: the changes of
 *    a process which is called `explorer.exe` are changes of the machine like
 *    the others, and an entry which no byte was written to is named by the
 *    event which reports that a file came into being.
 * 2. The difference reports the three entries as added entries of the preset
 *    directory of the user profile at the root of it, with the folder marked as
 *    a folder.
 * 3. The fill imports the two files into the layer root of the preset
 *    directory and creates the folder as a folder of the view, so the list of
 *    the preset directory holds both files.
 */
TEST_F(E2E_RealtimeCapture, CapturesTheEntriesTheShellCreated)
{
    if (!appbox::RealtimeCaptureSupported())
    {
        GTEST_SKIP() << "the machine does not carry the kernel events of the capture";
    }
    if (!appbox::IsProcessElevated())
    {
        GTEST_SKIP() << "the realtime capture needs an elevated process";
    }

    const auto profile = GetKnownFolderPath(L"#USERPROFILE#", false);
    const auto fragment = UniqueFragment();

    const auto shell_file = profile + L"\\appbox-realtime-shell-" + fragment + L".txt";
    const auto blank_file = profile + L"\\appbox-realtime-blank-" + fragment + L".txt";
    const auto empty_folder = profile + L"\\appbox-realtime-empty-" + fragment;

    RootEntryCleanup cleanup;
    cleanup.WatchFile(shell_file);
    cleanup.WatchFile(blank_file);
    cleanup.WatchFolder(empty_folder);

    std::vector<appbox::SnapshotEnvironmentEntry> environment_before;
    std::string                                   error;
    ASSERT_TRUE(appbox::ReadPersistedEnvironment(environment_before, error));

    appbox::RealtimeCaptureRecorder recorder;
    const auto                      options = WatchTheCaseOptions();
    if (!recorder.Start(options, appbox::RealtimeCaptureProgressCallback{}, error))
    {
        GTEST_SKIP() << "the recording could not be started: " << error;
    }

    ASSERT_TRUE(CreateABlankFileWithTheShellImage(GetCWD(), shell_file))
        << "the blank file of the process which carries the name of the shell was not created";
    ASSERT_TRUE(CreateABlankFile(blank_file));
    ASSERT_TRUE(::CreateDirectoryW(empty_folder.c_str(), nullptr) != 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    recorder.RequestStop();
    ASSERT_TRUE(recorder.Join(15000));

    const auto events = recorder.TakeEvents();
    ASSERT_FALSE(events.empty());

    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::FileCreated, shell_file))
        << "the blank file the shell created was not recorded";
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::FileCreated, blank_file))
        << "the blank file was not recorded";
    EXPECT_TRUE(HasEvent(events, appbox::RealtimeCaptureEvent::Kind::FileCreated, empty_folder))
        << "the empty folder was not recorded";

    appbox::SnapshotDiff      diff;
    appbox::CaptureFillReport report;
    ASSERT_TRUE(appbox::HarvestRealtimeEvents(events, environment_before, options, diff, report, error)) << error;

    const auto shell_name = std::filesystem::path(shell_file).filename().wstring();
    const auto blank_name = std::filesystem::path(blank_file).filename().wstring();
    const auto folder_name = std::filesystem::path(empty_folder).filename().wstring();

    EXPECT_TRUE(HasAddedFile(diff, shell_name, false));
    EXPECT_TRUE(HasAddedFile(diff, blank_name, false));
    EXPECT_TRUE(HasAddedFile(diff, folder_name, true));

    /*
     * The fill runs on the entries of the case alone: the recording observes the
     * whole machine, so the difference holds the changes of every program which
     * ran meanwhile as well.
     */
    appbox::SnapshotDiff own;
    for (const auto& entry : diff.added_files)
    {
        if (entry.preset_id == "user_profile" &&
            (entry.path == shell_name || entry.path == blank_name || entry.path == folder_name))
        {
            own.added_files.push_back(entry);
        }
    }
    ASSERT_EQ(own.added_files.size(), 3u);

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport own_report;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(own, model, registry, environment, own_report, {}, error)) << error;

    EXPECT_TRUE(own_report.errors.empty());
    EXPECT_TRUE(own_report.ignored.empty());

    EXPECT_TRUE(model.HasImportedFileAt("user_profile", shell_name));
    EXPECT_TRUE(model.HasImportedFileAt("user_profile", blank_name));
    EXPECT_TRUE(model.IsDirectoryOfView("user_profile", folder_name));

    /*
     * The list of a preset directory holds the files which were imported into
     * the layer root, which is where the entries of the case live.
     */
    bool shell_listed = false;
    bool blank_listed = false;
    for (const auto& file : model.FilesOf("user_profile", L""))
    {
        shell_listed = shell_listed || file.file_name == shell_name;
        blank_listed = blank_listed || file.file_name == blank_name;
    }
    EXPECT_TRUE(shell_listed) << "the blank file the shell created is not listed";
    EXPECT_TRUE(blank_listed) << "the blank file is not listed";
}
