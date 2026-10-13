#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/TestKnownFolder.hpp"
#include "launcher/Config.hpp"
#include "WString.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Launcher_StartupScript;
using namespace appbox::test;

namespace
{

/** Name of the folder of a case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_Script";

/** Marker the script of a case writes. */
constexpr char kMarker[] = "appbox-script-ran";

/** Name of the file a script writes the marker into. */
constexpr wchar_t kMarkerFileName[] = L"started.txt";

/** Exit code the script of a case returns. */
constexpr std::uint32_t kScriptExitCode = 7;

/**
 * Batch file which writes the marker beside itself and leaves with a known exit
 * code. `%~dp0` is the directory of the script, which is a folder of the
 * sandbox view, so the write lands in the overlay of the sandbox.
 */
constexpr char kBatchScript[] = "@echo off\n"
                                ">\"%~dp0started.txt\" echo appbox-script-ran\n"
                                "exit /b 7\n";

/**
 * @brief Build the filesystem of a case and its launcher configuration.
 *
 * The layer carries the folder of the case and the script itself, both below
 * `#USERPROFILE#`, so the script lives in the sandbox view only: the host
 * folder holds no script of the case.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @param[in] script_name Name of the script inside the folder of the case.
 * @param[in] script Content of the script.
 * @return The directories of the case and the configuration which starts the
 *         script.
 */
std::pair<FsRoot, appbox::LauncherConfig> BuildCase(const std::filesystem::path& root, const std::wstring& script_name,
                                                    const std::string& script)
{
    /* clang-format off */
    FsRoot tree(root, {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(script_name, script)
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    appbox::LauncherStartup startup;
    startup.trigger = appbox::WideToUTF8(std::filesystem::path(script_name).stem().wstring());
    startup.auto_start = true;
    startup.executable = appbox::WideToUTF8(std::wstring(L"#USERPROFILE#\\") + kFolderName + L"\\" + script_name);
    config.startups.push_back(std::move(startup));

    return { tree, config };
}

/**
 * @brief Pin the layer of a case to `Write Copy`.
 *
 * The mode keeps the write of a script inside the sandbox: the marker lands in
 * the overlay of the sandbox instead of the folder of the host, which is what
 * makes it a proof that the script ran inside the isolation.
 *
 * @param[in] cwd The current working directory of the case.
 * @return true when the isolation file was written.
 */
bool PinTheLayerToWriteCopy(const std::filesystem::path& cwd)
{
    return WriteFsIsolationFile(cwd, {
                                         { L"#USERPROFILE#", appbox::FilesystemEntryKind::Directory,
                                          appbox::FilesystemIsolation::WriteCopy }
    });
}

/**
 * @brief Get the path of the marker inside the overlay of the sandbox.
 * @param[in] cwd The current working directory of the case.
 * @return The path of the marker.
 */
std::wstring MarkerOverlayPath(const std::wstring& cwd)
{
    return cwd + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) + L"\\" + kFolderName + L"\\" +
           kMarkerFileName;
}

/**
 * @brief Get the path of the marker inside the folder of the host.
 * @return The path of the marker.
 */
std::wstring MarkerHostPath()
{
    return GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName + L"\\" + kMarkerFileName;
}

/**
 * @brief Check that the script of a case ran inside the sandbox.
 *
 * @param[in] run The run of the launcher.
 * @param[in] cwd The current working directory of the case.
 */
void ExpectTheScriptRanInsideTheSandbox(const appbox::test::ProgramRun& run, const std::wstring& cwd)
{
    EXPECT_EQ(run.exit_code, kScriptExitCode) << "the launcher did not report the exit code of the script";

    /* The script wrote the marker into the overlay of the sandbox ... */
    std::string marker;
    ASSERT_EQ(ReadFileFull(MarkerOverlayPath(cwd), marker), static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_NE(marker.find(kMarker), std::string::npos) << marker;

    /* ... and never into the folder of the host, which holds no such file. */
    EXPECT_FALSE(std::filesystem::exists(MarkerHostPath()));
}

} // namespace

/**
 * Condition:
 * 1. The layer of the case holds `#USERPROFILE#\AppBoxTest_Script\run.bat`, a
 *    script which exists in the sandbox view only and which writes a marker
 *    beside itself and returns 7.
 * 2. The layer is pinned to `Write Copy`, so the write of the script lands in
 *    the overlay of the sandbox.
 * 3. The configuration starts the batch file as its only startup file.
 *
 * Expected:
 * 1. The launcher runs the batch file through the command interpreter of the
 *    host, which reports the exit code of the script.
 * 2. The script ran inside the sandbox: the marker is in the overlay of the
 *    sandbox and not in the folder of the host.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Launcher_StartupScript, ABatchFileIsRunByTheCommandInterpreter)
{
    auto [tree, config] = BuildCase(GetCWD(), L"run.bat", kBatchScript);
    ASSERT_TRUE(PinTheLayerToWriteCopy(GetCWD()));

    ExpectTheScriptRanInsideTheSandbox(ProbeProgramRun(GetCWDString(), config), GetCWDString());

    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. The layer of the case holds `#USERPROFILE#\AppBoxTest_Script\run.cmd`,
 *    which writes a marker beside itself and returns 7.
 * 2. The layer is pinned to `Write Copy`.
 * 3. The configuration starts the command script as its only startup file.
 *
 * Expected:
 * 1. The launcher runs the command script through the command interpreter of
 *    the host, which reports the exit code of the script.
 * 2. The script ran inside the sandbox: the marker is in the overlay of the
 *    sandbox and not in the folder of the host.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Launcher_StartupScript, ACommandScriptIsRunByTheCommandInterpreter)
{
    auto [tree, config] = BuildCase(GetCWD(), L"run.cmd", kBatchScript);
    ASSERT_TRUE(PinTheLayerToWriteCopy(GetCWD()));

    ExpectTheScriptRanInsideTheSandbox(ProbeProgramRun(GetCWDString(), config), GetCWDString());

    ASSERT_TRUE(tree.Verify());
}

/*
 * A PowerShell script has no case of its own, because the sandbox refuses to
 * run Windows PowerShell: the launcher builds the invocation of the script (the
 * unit test of `launcher/utils/Shell.cpp` pins that), and the sandbox lets the
 * interpreter abort while it starts. Windows PowerShell is a .NET Framework
 * application, and the runtime reports `E_ACCESSDENIED` and leaves with
 * `0xFFFF0000` inside the sandbox, which no other program shows: `csc.exe`,
 * which is a .NET Framework application as well, runs inside the same sandbox.
 * The limitation is documented in `docs/Launcher.md`.
 */

/**
 * Condition:
 * 1. The configuration names a startup file which is not startable at all, so
 *    the launcher cannot build the invocation of the file.
 *
 * Expected:
 * 1. No process is started and the launcher reports the failure through a non
 *    zero exit code.
 */
TEST_F(E2E_Launcher_StartupScript, AFileWhichCannotBeStartedRefusesTheRun)
{
    /* clang-format off */
    FsRoot tree(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {})
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    appbox::LauncherStartup startup;
    startup.trigger = "notes";
    startup.auto_start = true;
    startup.executable = "#USERPROFILE#\\notes.txt";
    config.startups.push_back(std::move(startup));

    const auto run = ProbeProgramRun(GetCWDString(), config);

    EXPECT_NE(run.exit_code, 0u);
}
