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

typedef appbox::test::CommonFixture E2E_Launcher_Shell;
using namespace appbox::test;

namespace
{

/** Name of the folder of a case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_Shell";

/**
 * @brief Build the launcher configuration of a case.
 *
 * The configuration carries the filesystem of the sandbox the launcher mounts:
 * an empty upper layer and a lower layer below `filesystem`, which holds the
 * folder the shell of a case writes into. The folder exists in the view of the
 * sandbox only, so a case which writes into it pins the layer to `Write Copy`
 * and the write lands in the overlay of the sandbox instead of the folder of
 * the host, which is what makes it a proof that the command of the shell ran
 * inside the isolation.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @return The configuration without any startup file.
 */
appbox::LauncherConfig BuildConfig(const std::filesystem::path& root)
{
    /* clang-format off */
    auto tree = FsRoot(root, {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {})
            })
        })
    });
    /* clang-format on */

    return tree.Build();
}

/**
 * @brief Append one startup file to a configuration.
 *
 * The arguments of the file carry its marker, which the probe process of the
 * case reports back: the markers tell which startup files the launcher started.
 *
 * @param[in,out] config Configuration to extend.
 * @param[in] trigger Trigger and marker of the startup file.
 * @param[in] auto_start Whether the launcher starts the file on its own.
 */
void AddStartup(appbox::LauncherConfig& config, const std::string& trigger, bool auto_start)
{
    appbox::LauncherStartup startup;
    startup.trigger = trigger;
    startup.auto_start = auto_start;
    startup.arguments = { "--startup_marker", trigger };
    config.startups.push_back(std::move(startup));
}

/**
 * @brief Build a configuration with two auto start files and one manual file.
 * @param[in] root Root directory of the filesystem of the case.
 * @return The configuration with three startup files.
 */
appbox::LauncherConfig BuildThreeStartups(const std::filesystem::path& root)
{
    auto config = BuildConfig(root);
    AddStartup(config, "one", true);
    AddStartup(config, "two", true);
    AddStartup(config, "manual", false);
    return config;
}

} // namespace

/**
 * Condition:
 * 1. The configuration holds three startup files, two of them marked for auto
 *    start.
 * 2. The launcher runs with `--X-AppBox-Shell exit 42`.
 *
 * Expected:
 * 1. The shell of the host runs the command and the launcher exits with the exit
 *    code of that shell.
 * 2. No startup file is started, not even an auto start one: the shell
 *    replaces the application of the configuration.
 */
TEST_F(E2E_Launcher_Shell, CommandRunsAndStartupsAreIgnored)
{
    const auto config = BuildThreeStartups(GetCWD());

    const auto run = ProbeShellRun(GetCWDString(), config, { "exit", "42" });

    EXPECT_EQ(run.exit_code, 42u);
    EXPECT_TRUE(run.reported.empty());
}

/**
 * Condition:
 * 1. The lower layer of the filesystem holds the folder `#USERPROFILE#\
 *    AppBoxTest_Shell`, which exists in the view of the sandbox only.
 * 2. The layer is pinned to `Write Copy`, so the write of the command lands in
 *    the overlay of the sandbox instead of the folder of the host.
 * 3. The launcher runs with `--X-AppBox-Shell echo hello> <folder>\shell.txt`,
 *    where the folder is the path of the known folder of the host.
 *
 * Expected:
 * 1. The shell runs the command and the launcher exits with zero.
 * 2. The command runs inside the sandbox: the file is written into the overlay
 *    of the sandbox instead of the folder of the host.
 * 3. No startup file is started and the resources of the application are
 *    untouched.
 */
TEST_F(E2E_Launcher_Shell, CommandRunsInsideTheSandbox)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {})
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    /*
     * The layer of the case is pinned to `Write Copy`, which is the mode a
     * path of the sandbox had before the default of the view became `Merge`:
     * the write of the command lands in the overlay of the sandbox and never
     * in the folder of the host.
     */
    ASSERT_TRUE(WriteFsIsolationFile(GetCWD(), {
                                                   { L"#USERPROFILE#", appbox::FilesystemEntryKind::Directory,
                                                    appbox::FilesystemIsolation::WriteCopy }
    }));

    const auto folder = GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
    const auto file = folder + L"\\shell.txt";

    const auto run = ProbeShellRun(GetCWDString(), config, { "echo", "hello>", appbox::WideToUTF8(file) });

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_TRUE(run.reported.empty());

    /* The command of the shell wrote into the overlay of the sandbox ... */
    {
        const auto overlay = GetCWDString() + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) +
                             L"\\" + kFolderName + L"\\shell.txt";

        std::string data;
        ASSERT_EQ(ReadFileFull(overlay, data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "hello\r\n");
    }

    /* ... and never into the folder of the host, which holds no such file. */
    EXPECT_FALSE(std::filesystem::exists(file));

    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. The configuration holds three startup files.
 * 2. The launcher runs with `--X-AppBox-Shell exit 0` and `--X-AppBox-Startup
 *    manual`.
 *
 * Expected:
 * 1. Neither the shell nor a startup file runs: the two options name different
 *    programs to run, so the run is refused.
 * 2. The launcher reports the failure through a non zero exit code.
 */
TEST_F(E2E_Launcher_Shell, ShellAndStartupAreMutuallyExclusive)
{
    const auto config = BuildThreeStartups(GetCWD());

    const auto run = ProbeShellRun(GetCWDString(), config, { "exit", "0" }, "manual");

    EXPECT_NE(run.exit_code, 0u);
    EXPECT_TRUE(run.reported.empty());
}
