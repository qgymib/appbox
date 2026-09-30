#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "launcher/Config.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Launcher_Startup;
using namespace appbox::test;

namespace
{

/**
 * @brief Build the launcher configuration of a case.
 *
 * The configuration carries the filesystem of the sandbox the launcher mounts:
 * an empty upper layer and a lower layer below `filesystem`, which is the
 * layout a packaged archive uses.
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
 * 2. The launcher runs without `--X-AppBox-Startup`.
 *
 * Expected:
 * 1. The launcher starts exactly the two auto start files.
 * 2. The file without the auto start flag is not started.
 * 3. The launcher exits with the exit code of the started files (zero).
 */
TEST_F(E2E_Launcher_Startup, AutoStartAll)
{
    const auto config = BuildThreeStartups(GetCWD());

    const auto run = ProbeStartupRun(GetCWDString(), config, "");

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "one", "two" }));
}

/**
 * Condition:
 * 1. The configuration holds three startup files, one of them is not marked
 *    for auto start.
 * 2. The launcher runs with `--X-AppBox-Startup manual`.
 *
 * Expected:
 * 1. Only the startup file with the trigger is started; the auto start files
 *    are not started.
 * 2. The launcher exits with the exit code of the started file (zero).
 */
TEST_F(E2E_Launcher_Startup, SelectedByTrigger)
{
    const auto config = BuildThreeStartups(GetCWD());

    const auto run = ProbeStartupRun(GetCWDString(), config, "manual");

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "manual" }));
}

/**
 * Condition:
 * 1. The configuration holds three startup files.
 * 2. The launcher runs with `--X-AppBox-Startup missing`, which no startup file
 *    uses.
 *
 * Expected:
 * 1. No startup file is started, not even the auto start ones.
 * 2. The launcher reports the failure through a non zero exit code.
 */
TEST_F(E2E_Launcher_Startup, UnknownTrigger)
{
    const auto config = BuildThreeStartups(GetCWD());

    const auto run = ProbeStartupRun(GetCWDString(), config, "missing");

    EXPECT_NE(run.exit_code, 0u);
    EXPECT_TRUE(run.started.empty());
}
