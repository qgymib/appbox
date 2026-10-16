#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/LauncherPath.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/SandboxDll.hpp"
#include "utils/SnapshotFile.hpp"
#include "utils/TestKnownFolder.hpp"
#include "src/core/PackService.hpp"
#include "src/core/SnapshotApply.hpp"
#include "src/core/SnapshotCapture.hpp"
#include "src/core/SnapshotDiff.hpp"
#include "src/core/SnapshotStore.hpp"
#include "src/core/ZipReader.hpp"
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

} // namespace

/**
 * Condition:
 * 1. The baseline of the case is the state of the machine before the
 *    installation, which is a snapshot file whose phase before the change holds
 *    the folder of the installation alone: the content it receives is what the
 *    capture has to report.
 * 2. A folder below `#USERPROFILE#` is created and a file is written into it,
 *    which is the installation the capture reports.
 * 3. The state after the installation is written into the phase after the
 *    change of the same snapshot file, the two phases are compared and the
 *    difference is filled into the models, which is the whole work of the last
 *    step of the wizard of the capture.
 * 4. The session is packed, the archive is extracted into the directory of the
 *    case and the folder of the host is removed afterwards.
 * 5. The packaged application reads the file the capture imported.
 *
 * Expected:
 * 1. The folder which existed before the capture becomes a folder of the view
 *    and the file the installation wrote is imported at its own path, so the
 *    entries of the difference travel without the rest of the folder.
 * 2. The archive carries the file, so it is readable although the folder of the
 *    host does not exist anymore.
 */
TEST_F(E2E_Snapshot, CapturedFolderTravelsIntoTheArchive)
{
    const auto& launcher = LauncherPath();
    if (launcher.empty())
    {
        GTEST_SKIP() << "the run was started without the path of the launcher";
    }

    const std::wstring name = L"appbox-e2e-capture-" + UniqueFragment();

    appbox::test::RealFsFolder folder(L"#USERPROFILE#", name);

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
     * The state before the installation is the baseline of the comparison: the
     * folder of the installation is part of it, its content is not. The case
     * writes that state itself, because the state of the machine which runs the
     * case is not reproducible.
     */
    appbox::test::SnapshotPhaseContent before;
    before.files.push_back(appbox::SnapshotFileEntry{ "user_profile", name, true, 0, 0 });
    ASSERT_TRUE(appbox::test::WriteSnapshotPhase(snapshot_path, appbox::SnapshotPhase::Before, before, error)) << error;

    /* The installation writes a file into the folder, which is the change. */
    ASSERT_TRUE(folder.WriteFile(L"bin\\app.exe", "PACKED-BY-THE-CAPTURE"));

    {
        const auto writer = appbox::SnapshotWriter::Open(snapshot_path, error);
        ASSERT_NE(writer, nullptr) << error;
        ASSERT_TRUE(writer->BeginPhase(appbox::SnapshotPhase::After, error)) << error;
        ASSERT_TRUE(appbox::CaptureSnapshot(options, *writer, nullptr, error)) << error;
        ASSERT_TRUE(writer->CommitPhase(error)) << error;
    }

    appbox::SnapshotDiff diff;
    ASSERT_TRUE(appbox::DiffSnapshotPhases(snapshot_path, diff, error)) << error;
    ASSERT_FALSE(diff.added_files.empty());

    appbox::PackModel         model;
    appbox::RegistryModel     registry;
    appbox::EnvironmentModel  environment;
    appbox::CaptureFillReport report;

    /*
     * The difference is what the last step of the wizard fills into the models:
     * the folder which existed before the capture becomes a folder of the view
     * and the file the installation wrote is imported at its own path, so the
     * entries of the difference travel without the rest of the folder.
     */
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, {}, error)) << error;
    EXPECT_TRUE(report.errors.empty());
    EXPECT_TRUE(report.ignored.empty());

    /*
     * The difference holds the changes of the machine and not the entries of the
     * case alone, because the baseline of the case describes the folder of the
     * case only: the two entries of the case were written, and the case checks
     * them on the models below.
     */
    EXPECT_GE(report.applied, 2u);

    /* The folder is an entry of the view and not an import of the host folder. */
    EXPECT_FALSE(model.HasImportedFileAt("user_profile", name));
    ASSERT_TRUE(model.HasImportedFileAt("user_profile", name + L"\\bin\\app.exe"));
    ASSERT_TRUE(model.AddStartupFile("user_profile", name, L"bin\\app.exe", true, error)) << error;

    /* The session is packed and extracted into the directory of the case. */
    const auto launcher_bytes = ReadFileBytes(launcher);
    const auto sandbox32_bytes = ReadFileBytes(Sandbox32DllPath());
    const auto sandbox64_bytes = ReadFileBytes(Sandbox64DllPath());
    ASSERT_FALSE(launcher_bytes.empty());

    appbox::PackPayloads payloads;
    payloads.launcher_bytes = launcher_bytes.data();
    payloads.launcher_size = launcher_bytes.size();
    payloads.sandbox32_bytes = sandbox32_bytes.data();
    payloads.sandbox32_size = sandbox32_bytes.size();
    payloads.sandbox64_bytes = sandbox64_bytes.data();
    payloads.sandbox64_size = sandbox64_bytes.size();

    const auto zip_path = GetCWD() / (name + L".zip");
    ASSERT_EQ(appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(), environment,
                           appbox::ApplicationMetadata(), appbox::StartupScreen{}, payloads,
                           appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr),
              "");
    ASSERT_EQ(appbox::ExtractArchive(zip_path.wstring(), GetCWDString()), "");

    /*
     * The layer of the archive carries the file the installation wrote at the
     * path of the view the capture filled, so the entry is part of the archive
     * although its folder is a folder of the view and not an import.
     */
    const auto layer = GetCWD() / L"app" / L"filesystem" / L"#USERPROFILE#" / name;
    EXPECT_TRUE(std::filesystem::exists(layer / L"bin" / L"app.exe"));
    EXPECT_TRUE(std::filesystem::exists(layer / L"bin"));

    /*
     * The folder of the host is removed, so only the layer of the archive can
     * answer the read of the packaged application.
     */
    std::error_code ec;
    std::filesystem::remove_all(folder.Get(), ec);
    ASSERT_FALSE(std::filesystem::exists(folder.Get()));

    const auto path = GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + name + L"\\bin\\app.exe";

    ProtocolReadFileFull::Req req;
    req.FileName = appbox::WideToUTF8(path);

    const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), appbox::LauncherConfig{}).get<ProtocolReadFileFull::Rsp>();

    ASSERT_EQ(rsp.code, 0u);
    EXPECT_EQ(rsp.data, "PACKED-BY-THE-CAPTURE");
}
