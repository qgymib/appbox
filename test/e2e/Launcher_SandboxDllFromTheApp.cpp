#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "launcher/Config.hpp"
#include "SandboxLayout.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Launcher_SandboxDllFromTheApp;
using namespace appbox::test;

namespace
{

/**
 * @brief Build the launcher configuration of a case.
 *
 * The configuration carries the filesystem of the sandbox the launcher mounts
 * and one startup file which starts the probe process of the case.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @param[in] trigger Trigger and marker of the startup file.
 * @return The configuration with one auto start file.
 */
appbox::LauncherConfig BuildConfig(const std::filesystem::path& root, const std::string& trigger)
{
    /* clang-format off */
    auto tree = FsRoot(root, {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"filesystem\\#USERPROFILE#", {}) })
    });
    /* clang-format on */

    auto config = tree.Build();

    appbox::LauncherStartup startup;
    startup.trigger = trigger;
    startup.auto_start = true;
    startup.arguments = { "--startup_marker", trigger };
    config.startups.push_back(std::move(startup));

    return config;
}

} // namespace

/**
 * Condition:
 * 1. The resource root of the case carries the two sandbox injection modules
 *    and the state root is empty.
 * 2. The launcher runs with a startup file marked for auto start.
 *
 * Expected:
 * 1. The run starts the startup file inside the sandbox, so the modules of the
 *    resource root were injected.
 * 2. The state root carries no module afterwards: the launcher injects the
 *    modules from the resource root instead of writing a copy of its own.
 */
TEST_F(E2E_Launcher_SandboxDllFromTheApp, TheRunInjectsFromTheResourceRoot)
{
    const auto config = BuildConfig(GetCWD(), "one");

    const auto run = ProbeStartupRun(GetCWDString(), config, "");

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "one" }));

    const std::filesystem::path app(GetCWD() / appbox::layout::kAppDirNameW);
    const std::filesystem::path state(GetCWD() / appbox::layout::kStateDirNameW);

    EXPECT_TRUE(std::filesystem::is_regular_file(app / appbox::layout::kSandbox32DllNameW));
    EXPECT_TRUE(std::filesystem::is_regular_file(app / appbox::layout::kSandbox64DllNameW));

    EXPECT_FALSE(std::filesystem::exists(state / appbox::layout::kSandbox32DllNameW));
    EXPECT_FALSE(std::filesystem::exists(state / appbox::layout::kSandbox64DllNameW));
}
