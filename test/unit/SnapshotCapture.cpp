#include <gtest/gtest.h>
#include "src/core/PresetDirectory.hpp"
#include "src/core/RegistryModel.hpp"
#include "src/core/SnapshotCapture.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/SnapshotFile.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Generate a unique name fragment for temporary folders.
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
 * @param[in] name Base name of the folder.
 * @return The unique folder name.
 */
std::wstring UniqueFolderName(const wchar_t* name)
{
    return std::wstring(name) + L"-" + UniqueFragment();
}

/**
 * @brief Build a capture which reads one folder below the user profile.
 * @param[in] folder Folder to read.
 * @return The options of the capture.
 */
appbox::CaptureOptions OptionsFor(const std::wstring& folder)
{
    appbox::CaptureOptions options;
    options.filesystem_roots.push_back(appbox::CaptureFilesystemRoot{ "user_profile", folder });
    return options;
}

/**
 * @brief Read the state of the machine into the phase before the change of a
 *        new snapshot file.
 *
 * The call is what the `Capture Before` command of the packer does: a file is
 * created, the state is written into it entry by entry and the phase is
 * committed once the walk is over.
 *
 * @param[in] options The roots to walk through.
 * @param[in] path Path of the snapshot file to create.
 * @param[in] progress Progress callback, may be empty.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool CaptureBefore(const appbox::CaptureOptions& options, const std::wstring& path,
                   const appbox::CaptureProgressCallback& progress, std::string& error)
{
    const auto writer = appbox::SnapshotWriter::Create(path, error);
    if (writer == nullptr)
    {
        return false;
    }

    if (!writer->BeginPhase(appbox::SnapshotPhase::Before, error))
    {
        return false;
    }

    if (!appbox::CaptureSnapshot(options, *writer, progress, error))
    {
        return false;
    }

    return writer->CommitPhase(error);
}

/**
 * @brief Count the entries of a phase which are files and not directories.
 * @param[in] content The entries of the phase.
 * @return The number of the entries which are files.
 */
std::size_t RegularFileCount(const appbox::test::SnapshotPhaseContent& content)
{
    return static_cast<std::size_t>(
        std::count_if(content.files.begin(), content.files.end(),
                      [](const appbox::SnapshotFileEntry& entry) { return !entry.directory; }));
}

} // namespace

TEST(Unit_SnapshotCapture, CapturesTheFolderItIsPointedAt)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", UniqueFolderName(L"appbox-snapshot-capture"));
    ASSERT_TRUE(folder.WriteFile(L"bin\\app.exe", "hello"));
    ASSERT_TRUE(folder.WriteFile(L"readme.txt", "12345"));

    const appbox::test::TempSnapshotFile file(L"capture.db");

    std::string error;
    ASSERT_TRUE(CaptureBefore(OptionsFor(folder.Get().wstring()), file.Get(), nullptr, error)) << error;

    appbox::test::SnapshotPhaseContent content;
    ASSERT_TRUE(appbox::test::ReadSnapshotPhase(file.Get(), appbox::SnapshotPhase::Before, content, error)) << error;

    const auto root = folder.Get().filename().wstring();

    /*
     * The scanned folder itself is an entry of the view when it hangs below
     * the preset directory, because that is the folder a difference has to
     * import.
     */
    const auto* scanned = appbox::test::FindFileEntry(content, root);
    ASSERT_NE(scanned, nullptr) << "the scanned folder is missing";
    EXPECT_TRUE(scanned->directory);
    EXPECT_EQ(scanned->preset_id, "user_profile");

    const auto* bin = appbox::test::FindFileEntry(content, root + L"\\bin");
    ASSERT_NE(bin, nullptr);
    EXPECT_TRUE(bin->directory);
    EXPECT_EQ(bin->size, 0u);

    const auto* program = appbox::test::FindFileEntry(content, root + L"\\bin\\app.exe");
    ASSERT_NE(program, nullptr);
    EXPECT_FALSE(program->directory);
    EXPECT_EQ(program->size, static_cast<std::uint64_t>(5));
    EXPECT_NE(program->write_time, 0);

    const auto* readme = appbox::test::FindFileEntry(content, root + L"\\readme.txt");
    ASSERT_NE(readme, nullptr);
    EXPECT_EQ(readme->size, static_cast<std::uint64_t>(5));

    EXPECT_EQ(RegularFileCount(content), static_cast<std::size_t>(2));
}

TEST(Unit_SnapshotCapture, CapturesAnEmptyFolderAsItself)
{
    const appbox::test::RealFsFolder     folder(L"#USERPROFILE#", UniqueFolderName(L"appbox-snapshot-capture-empty"));
    const appbox::test::TempSnapshotFile file(L"empty.db");

    std::string error;
    ASSERT_TRUE(CaptureBefore(OptionsFor(folder.Get().wstring()), file.Get(), nullptr, error)) << error;

    appbox::test::SnapshotPhaseContent content;
    ASSERT_TRUE(appbox::test::ReadSnapshotPhase(file.Get(), appbox::SnapshotPhase::Before, content, error)) << error;

    EXPECT_NE(appbox::test::FindFileEntry(content, folder.Get().filename().wstring()), nullptr);
    EXPECT_EQ(RegularFileCount(content), static_cast<std::size_t>(0));
}

TEST(Unit_SnapshotCapture, SkipsAReparsePoint)
{
    appbox::test::RealFsFolder folder(L"#USERPROFILE#", UniqueFolderName(L"appbox-snapshot-capture-link"));
    ASSERT_TRUE(folder.WriteFile(L"target\\file.txt", "x"));
    ASSERT_TRUE(folder.CreateJunction(L"link", (folder.Get() / L"target").wstring()));

    const appbox::test::TempSnapshotFile file(L"link.db");

    std::string error;
    ASSERT_TRUE(CaptureBefore(OptionsFor(folder.Get().wstring()), file.Get(), nullptr, error)) << error;

    appbox::test::SnapshotPhaseContent content;
    ASSERT_TRUE(appbox::test::ReadSnapshotPhase(file.Get(), appbox::SnapshotPhase::Before, content, error)) << error;

    const auto root = folder.Get().filename().wstring();

    /*
     * The junction is not part of the snapshot at all: walking into it would
     * record the folder it names a second time.
     */
    EXPECT_EQ(appbox::test::FindFileEntry(content, root + L"\\link"), nullptr);
    EXPECT_NE(appbox::test::FindFileEntry(content, root + L"\\target\\file.txt"), nullptr);
}

TEST(Unit_SnapshotCapture, LeavesOutTheFileItWasToldToSkip)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", UniqueFolderName(L"appbox-snapshot-capture-skip"));
    ASSERT_TRUE(folder.WriteFile(L"keep.txt", "keep"));
    ASSERT_TRUE(folder.WriteFile(L"skip.db", "skip"));

    /*
     * The snapshot file of a capture may live below the folder which is read,
     * so the run leaves it out: a snapshot never describes itself.
     */
    auto options = OptionsFor(folder.Get().wstring());
    options.excluded_paths.push_back((folder.Get() / L"skip.db").wstring());

    const appbox::test::TempSnapshotFile file(L"skip.db");

    std::string error;
    ASSERT_TRUE(CaptureBefore(options, file.Get(), nullptr, error)) << error;

    appbox::test::SnapshotPhaseContent content;
    ASSERT_TRUE(appbox::test::ReadSnapshotPhase(file.Get(), appbox::SnapshotPhase::Before, content, error)) << error;

    const auto root = folder.Get().filename().wstring();
    EXPECT_NE(appbox::test::FindFileEntry(content, root + L"\\keep.txt"), nullptr);
    EXPECT_EQ(appbox::test::FindFileEntry(content, root + L"\\skip.db"), nullptr);
    EXPECT_EQ(RegularFileCount(content), static_cast<std::size_t>(1));
}

TEST(Unit_SnapshotCapture, RejectsAFolderOutsideThePresetDirectory)
{
    appbox::CaptureOptions options;
    options.filesystem_roots.push_back(appbox::CaptureFilesystemRoot{ "user_profile", L"C:\\Windows" });

    const appbox::test::TempSnapshotFile file(L"outside.db");

    std::string error;
    EXPECT_FALSE(CaptureBefore(options, file.Get(), nullptr, error));
    EXPECT_FALSE(error.empty());
}

TEST(Unit_SnapshotCapture, RejectsAnUnknownPresetDirectory)
{
    appbox::CaptureOptions options;
    options.filesystem_roots.push_back(appbox::CaptureFilesystemRoot{ "no_such_preset", L"C:\\Windows" });

    const appbox::test::TempSnapshotFile file(L"unknown.db");

    std::string error;
    EXPECT_FALSE(CaptureBefore(options, file.Get(), nullptr, error));
    EXPECT_FALSE(error.empty());
}

TEST(Unit_SnapshotCapture, RejectsAnUnknownRegistryRoot)
{
    appbox::CaptureOptions options;
    options.registry_roots.push_back(L"HKEY_NO_SUCH_ROOT");

    const appbox::test::TempSnapshotFile file(L"root.db");

    std::string error;
    EXPECT_FALSE(CaptureBefore(options, file.Get(), nullptr, error));
    EXPECT_FALSE(error.empty());
}

TEST(Unit_SnapshotCapture, ReadsTheRegistryAndTheEnvironment)
{
    /*
     * The walk of a root key is the only way to cover the registry part: the
     * assertions stay weak on purpose, because the content of the registry of
     * the machine which runs the case is not known.
     */
    appbox::CaptureOptions options;
    options.registry_roots.push_back(L"HKEY_CURRENT_USER");

    const appbox::test::TempSnapshotFile file(L"registry.db");

    std::string error;
    ASSERT_TRUE(CaptureBefore(options, file.Get(), nullptr, error)) << error;

    appbox::test::SnapshotPhaseContent content;
    ASSERT_TRUE(appbox::test::ReadSnapshotPhase(file.Get(), appbox::SnapshotPhase::Before, content, error)) << error;

    ASSERT_FALSE(content.registry_keys.empty());
    EXPECT_EQ(content.registry_keys.front(), L"HKEY_CURRENT_USER");
    EXPECT_FALSE(content.registry_values.empty());
    EXPECT_FALSE(content.environment.empty());

    /* The environment key of the user is part of the walk as well. */
    EXPECT_NE(std::find(content.registry_keys.begin(), content.registry_keys.end(), L"HKEY_CURRENT_USER\\Environment"),
              content.registry_keys.end());

    appbox::SnapshotPhaseCounts counts;
    const auto                  reader = appbox::SnapshotReader::Open(file.Get(), error);
    ASSERT_NE(reader, nullptr) << error;
    ASSERT_TRUE(reader->Counts(appbox::SnapshotPhase::Before, counts, error)) << error;
    EXPECT_EQ(counts.registry_keys, content.registry_keys.size());
    EXPECT_EQ(counts.registry_values, content.registry_values.size());
    EXPECT_EQ(counts.environment, content.environment.size());
}

TEST(Unit_SnapshotCapture, ReportsEveryStageOfTheCapture)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", UniqueFolderName(L"appbox-snapshot-capture-report"));
    ASSERT_TRUE(folder.WriteFile(L"file.txt", "x"));

    std::vector<appbox::CaptureProgress> reports;
    const auto                           collect = [&reports](const appbox::CaptureProgress& report) {
        reports.push_back(report);
        return true;
    };

    const appbox::test::TempSnapshotFile file(L"report.db");

    std::string error;
    ASSERT_TRUE(CaptureBefore(OptionsFor(folder.Get().wstring()), file.Get(), collect, error)) << error;

    ASSERT_FALSE(reports.empty());
    EXPECT_EQ(reports.front().stage, appbox::CaptureStage::Filesystem);

    bool saw_environment = false;
    for (const auto& report : reports)
    {
        saw_environment = saw_environment || report.stage == appbox::CaptureStage::Environment;
    }
    EXPECT_TRUE(saw_environment);

    /* The count of the recorded entries never moves backwards. */
    for (std::size_t index = 1; index < reports.size(); ++index)
    {
        EXPECT_GE(reports[index].done, reports[index - 1].done);
    }
}

TEST(Unit_SnapshotCapture, AbortsWhenTheCallbackAsksForIt)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", UniqueFolderName(L"appbox-snapshot-capture-abort"));
    ASSERT_TRUE(folder.WriteFile(L"file.txt", "x"));

    const auto stop = [](const appbox::CaptureProgress&) { return false; };

    const appbox::test::TempSnapshotFile file(L"abort.db");

    std::string error;
    EXPECT_FALSE(CaptureBefore(OptionsFor(folder.Get().wstring()), file.Get(), stop, error));
    EXPECT_EQ(error, appbox::kCaptureCancelledError);

    /* The phase was not marked, so the file holds no state a comparison reads. */
    const auto reader = appbox::SnapshotReader::Open(file.Get(), error);
    ASSERT_NE(reader, nullptr) << error;

    bool present = true;
    ASSERT_TRUE(reader->HasPhase(appbox::SnapshotPhase::Before, present, error)) << error;
    EXPECT_FALSE(present);
}

TEST(Unit_SnapshotCapture, TheDefaultRootsCoverTheWholeMachine)
{
    const auto roots = appbox::DefaultCaptureFilesystemRoots();
    ASSERT_FALSE(roots.empty());

    for (const auto& root : roots)
    {
        appbox::PresetDirectory preset;
        ASSERT_TRUE(appbox::FindPresetDirectory(root.preset_id, preset)) << root.preset_id;
        EXPECT_EQ(root.scan_path, preset.real_path);
    }

    EXPECT_EQ(roots.size(), appbox::PresetDirectories().size());

    const auto registry = appbox::DefaultCaptureRegistryRoots();
    EXPECT_EQ(registry, appbox::RegistryRootKeyNames());
}
