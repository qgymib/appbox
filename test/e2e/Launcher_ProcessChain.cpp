#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "launcher/Config.hpp"
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Launcher_ProcessChain;
using namespace appbox::test;

namespace
{

/**
 * @brief Build the launcher configuration of a case.
 *
 * The configuration carries the layout of a packed archive: the state of the
 * sandbox below `data` and the read-only resources below `app`.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @return The configuration without any startup file.
 */
appbox::LauncherConfig BuildConfig(const std::filesystem::path& root)
{
    /* clang-format off */
    auto tree = FsRoot(root, {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"filesystem\\#USERPROFILE#", {}) })
    });
    /* clang-format on */

    return tree.Build();
}

/**
 * @brief Append the startup file of a case.
 *
 * The arguments of the file carry its marker, which the probe process of the
 * case reports back, so a case can tell a run which started the file from a run
 * which did not.
 *
 * @param[in,out] launcher_config Configuration to extend.
 */
void AddStartup(appbox::LauncherConfig& launcher_config)
{
    appbox::LauncherStartup startup;
    startup.trigger = "app";
    startup.auto_start = true;
    startup.arguments = { "--startup_marker", "app" };
    launcher_config.startups.push_back(std::move(startup));
}

} // namespace

/**
 * Condition:
 * 1. The configuration holds one auto start file, which the launcher starts as
 *    the probe process inside the sandbox.
 * 2. The case records the process id of the launcher it started and the probe
 *    process reports the process which created it.
 *
 * Expected:
 * 1. The startup file is started.
 * 2. The process which created the startup file is the launcher itself: the
 *    launcher injects the sandbox into the startup file and creates it
 *    directly, instead of starting a process of its own in between.
 */
TEST_F(E2E_Launcher_ProcessChain, TheStartupFileIsCreatedByTheLauncher)
{
    auto launcher_config = BuildConfig(GetCWD());
    AddStartup(launcher_config);

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "");

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    ASSERT_EQ(run.reports.size(), 1u) << "the startup file reported " << run.reports.size() << " time(s)";
    EXPECT_NE(run.launcher_pid, 0u);

    const auto& report = run.reports[0];
    EXPECT_EQ(report.parent_pid, run.launcher_pid) << "the startup file was created by process " << report.parent_pid
                                                   << " instead of the launcher " << run.launcher_pid;
    EXPECT_NE(report.pid, report.parent_pid) << "the startup file reported itself as its own parent";
    EXPECT_FALSE(report.module_path.empty()) << "the probe reported no image path";
}
