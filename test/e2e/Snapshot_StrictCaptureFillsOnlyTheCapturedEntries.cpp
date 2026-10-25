#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/LauncherPath.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "src/core/PackService.hpp"
#include "src/core/SnapshotApply.hpp"
#include "src/core/SnapshotCapture.hpp"
#include "src/core/SnapshotDiff.hpp"
#include "src/core/SnapshotStore.hpp"
#include "ZipReader.hpp"
#include "WString.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

typedef appbox::test::CommonFixture E2E_Snapshot;
using namespace appbox::test;

namespace
{

/**
 * @brief Generate a unique name fragment for the folder of a case.
 * @return The unique fragment.
 */
std::wstring UniqueFragment()
{
    static unsigned counter = 0;
    const auto      ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_wstring(ticks) + L"-" + std::to_wstring(++counter);
}

/**
 * @brief Read a whole file.
 * @param[in] path Path of the file.
 * @return The content of the file, empty when it cannot be read.
 */
std::vector<char> ReadFileBytes(const std::wstring& path)
{
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/**
 * @brief Name of the marker which hides an entry of a layer of the host.
 */
constexpr const wchar_t* kDeleteSuffix = L".$APPBOX_DELETE$";

} // namespace

/**
 * Condition:
 * 1. A folder below `#USERPROFILE#` holds a file which was there before the
 *    capture and a folder which the installation removes.
 * 2. The state of the machine before the installation is read into the phase
 *    before the change of a snapshot file, the installation writes a file into
 *    the folder and removes the folder of the case, and the state after the
 *    change is read into the phase after the change.
 * 3. The two phases are compared and the difference is filled into the models,
 *    which is the whole work of the last step of the wizard of the capture.
 * 4. The session is packed, the archive is extracted into the directory of the
 *    case and the folder of the host is removed afterwards.
 * 5. The packaged application reads the file the installation wrote and tries to
 *    read the file the capture did not report.
 *
 * Expected:
 * 1. The difference names the file the installation wrote and the folder which
 *    is gone with the entry inside it.
 * 2. The archive carries the file the installation wrote at the path of the
 *    view of its preset directory and not the file next to it, which the
 *    recording never reported: a capture carries the entries of the difference
 *    and not the folder which holds them.
 * 3. The folder which is gone is hidden with a single marker, which is the
 *    marker of the folder: the entries below it are hidden with it and get no
 *    marker of their own.
 * 4. The packaged application reads the captured file although the folder of the
 *    host is gone, and it does not find the file which was never captured.
 */
TEST_F(E2E_Snapshot, StrictCaptureFillsOnlyTheCapturedEntries)
{
    const auto& launcher = LauncherPath();
    if (launcher.empty())
    {
        GTEST_SKIP() << "the run was started without the path of the launcher";
    }

    const std::wstring name = L"appbox-e2e-strict-" + UniqueFragment();

    appbox::test::RealFsFolder folder(L"#USERPROFILE#", name);
    ASSERT_TRUE(folder.WriteFile(L"kept.exe", "WAS-THERE-BEFORE"));
    ASSERT_TRUE(folder.WriteFile(L"gone\\inner.exe", "REMOVED-BY-THE-INSTALLATION"));

    const auto snapshot_path = (GetCWD() / (name + L".db")).wstring();

    appbox::CaptureOptions options;
    options.filesystem_roots.push_back(appbox::CaptureFilesystemRoot{ "user_profile", folder.Get().wstring() });

    /*
     * The snapshot file is the file which is in use, so the capture leaves it
     * out of the state it reads: a snapshot never describes itself.
     */
    options.excluded_paths.push_back(snapshot_path);

    std::string error;

    /*
     * Both states of the comparison are read from the machine, which is what a
     * capture does: the state before the installation holds the folder of the
     * case with the files which are in it now, so the comparison describes the
     * file the installation writes and the folder it removes and not the
     * entries which were there before.
     */
    {
        const auto writer = appbox::SnapshotWriter::Create(snapshot_path, error);
        ASSERT_NE(writer, nullptr) << error;
        ASSERT_TRUE(writer->BeginPhase(appbox::SnapshotPhase::Before, error)) << error;
        ASSERT_TRUE(appbox::CaptureSnapshot(options, *writer, nullptr, error)) << error;
        ASSERT_TRUE(writer->CommitPhase(error)) << error;
    }

    /* The installation writes a file and removes the folder of the case. */
    ASSERT_TRUE(folder.WriteFile(L"new.exe", "PACKED-BY-THE-CAPTURE"));
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::remove_all(folder.Get() / L"gone", ec));

    {
        const auto writer = appbox::SnapshotWriter::Open(snapshot_path, error);
        ASSERT_NE(writer, nullptr) << error;
        ASSERT_TRUE(writer->BeginPhase(appbox::SnapshotPhase::After, error)) << error;
        ASSERT_TRUE(appbox::CaptureSnapshot(options, *writer, nullptr, error)) << error;
        ASSERT_TRUE(writer->CommitPhase(error)) << error;
    }

    appbox::SnapshotDiff diff;
    ASSERT_TRUE(appbox::DiffSnapshotPhases(snapshot_path, diff, error)) << error;

    /*
     * The machine runs other programs while the case reads its two states, so
     * the difference may hold entries the case does not know: the case names its
     * own entries among them.
     */
    const auto added = [&diff](const std::wstring& path) {
        return std::any_of(diff.added_files.begin(), diff.added_files.end(),
                           [&path](const appbox::SnapshotDiffFile& entry) {
                               return entry.preset_id == "user_profile" && entry.path == path;
                           });
    };
    const auto removed = [&diff](const std::wstring& path) {
        return std::any_of(diff.removed_files.begin(), diff.removed_files.end(),
                           [&path](const appbox::SnapshotDiffFile& entry) {
                               return entry.preset_id == "user_profile" && entry.path == path;
                           });
    };

    EXPECT_TRUE(added(name + L"\\new.exe"));
    EXPECT_FALSE(added(name + L"\\kept.exe")) << "the file which was there before is not a change";
    EXPECT_TRUE(removed(name + L"\\gone"));
    EXPECT_TRUE(removed(name + L"\\gone\\inner.exe"));

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;

    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;
    EXPECT_TRUE(report.errors.empty());
    EXPECT_TRUE(report.ignored.empty());
    EXPECT_GE(report.applied, 2u);

    /*
     * The file of the installation is imported at its own path and the folder
     * which is gone is hidden with one marker, so the two entries of the folder
     * below it are one change and not two. The file which was there before the
     * capture is no entry of the difference, so it does not travel.
     */
    EXPECT_TRUE(model.HasImportedFileAt("user_profile", name + L"\\new.exe"));
    EXPECT_FALSE(model.HasImportedFileAt("user_profile", name + L"\\kept.exe"));
    EXPECT_TRUE(model.HasWhiteout("user_profile", name + L"\\gone"));
    EXPECT_EQ(model.AllWhiteouts().size(), 1u);

    ASSERT_TRUE(model.AddStartupFile("user_profile", name, L"new.exe", true, error)) << error;

    /* The session is packed and extracted into the directory of the case. */
    const auto launcher_bytes = ReadFileBytes(launcher);
    ASSERT_FALSE(launcher_bytes.empty());

    appbox::PackPayloads payloads;
    payloads.launcher_bytes = launcher_bytes.data();
    payloads.launcher_size = launcher_bytes.size();

    const auto zip_path = GetCWD() / (name + L".zip");
    ASSERT_EQ(appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(), environment,
                           appbox::ApplicationMetadata(), appbox::StartupScreen{}, payloads,
                           appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr),
              "");
    ASSERT_EQ(appbox::ExtractArchive(zip_path.wstring(), GetCWDString()), appbox::ZIPREADER_SUCCESS);

    /*
     * The layer of the archive carries the captured file, the marker of the
     * folder which is gone and neither the file which the capture did not report
     * nor a marker of the entry inside the hidden folder.
     */
    const auto layer = GetCWD() / L"app" / L"filesystem" / L"#USERPROFILE#" / name;
    EXPECT_TRUE(std::filesystem::exists(layer / L"new.exe"));
    EXPECT_TRUE(std::filesystem::exists(layer / (std::wstring(L"gone") + kDeleteSuffix)));
    EXPECT_FALSE(std::filesystem::exists(layer / L"kept.exe"));
    EXPECT_FALSE(std::filesystem::exists(layer / (std::wstring(L"gone\\inner.exe") + kDeleteSuffix)));

    /*
     * The folder of the host is removed, so only the layer of the archive can
     * answer the read of the packaged application.
     */
    ASSERT_TRUE(std::filesystem::remove_all(folder.Get(), ec));
    ASSERT_FALSE(std::filesystem::exists(folder.Get()));

    const auto root = GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + name;

    ProtocolReadFileFull::Req captured;
    captured.FileName = appbox::WideToUTF8(root + L"\\new.exe");
    const auto captured_rsp =
        ProbeReadFileFull.Call(captured, GetCWD(), appbox::LauncherConfig{}).get<ProtocolReadFileFull::Rsp>();
    ASSERT_EQ(captured_rsp.code, 0u);
    EXPECT_EQ(captured_rsp.data, "PACKED-BY-THE-CAPTURE");

    /* The file which the capture never reported is not part of the archive. */
    ProtocolReadFileFull::Req untouched;
    untouched.FileName = appbox::WideToUTF8(root + L"\\kept.exe");
    const auto untouched_rsp =
        ProbeReadFileFull.Call(untouched, GetCWD(), appbox::LauncherConfig{}).get<ProtocolReadFileFull::Rsp>();
    EXPECT_NE(untouched_rsp.code, 0u);
}
