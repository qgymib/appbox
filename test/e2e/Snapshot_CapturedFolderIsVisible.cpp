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
 *    installation, which is a snapshot file whose phase before the change is
 *    empty: the folder of the installation does not exist yet.
 * 2. A folder below `#USERPROFILE#` is created and a file is written into it,
 *    which is the installation the capture reports.
 * 3. The state after the installation is written into the phase after the
 *    change of the same snapshot file, the two phases are compared and the
 *    difference is written into the models, exactly like the `Capture and
 *    Diff` command of the packer does it.
 * 4. The session is packed, the archive is extracted into the directory of the
 *    case and the folder of the host is removed afterwards.
 * 5. The packaged application reads the file the capture imported.
 *
 * Expected:
 * 1. The folder of the installation was imported below its preset directory.
 * 2. The archive carries the content of the folder, so the file is readable
 *    although the folder of the host does not exist anymore.
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
     * folder of the installation is not part of it. The case writes that state
     * as an empty phase, because the state of the machine which runs the case
     * is not reproducible.
     */
    ASSERT_TRUE(appbox::test::WriteSnapshotPhase(snapshot_path, appbox::SnapshotPhase::Before,
                                                 appbox::test::SnapshotPhaseContent{}, error))
        << error;

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

    appbox::PackModel           model;
    appbox::RegistryModel       registry;
    appbox::EnvironmentModel    environment;
    appbox::SnapshotApplyReport report;
    ASSERT_TRUE(appbox::ApplySnapshotDiff(diff, model, registry, environment, report, error)) << error;
    ASSERT_EQ(report.imported_folders, 1u);

    appbox::ImportedFolder imported;
    ASSERT_TRUE(model.GetImport("user_profile", name, imported));
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
