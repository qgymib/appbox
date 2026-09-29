#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "loader/Config.hpp"
#include "SandboxLayout.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

typedef appbox::test::CommonFixture E2E_Loader_MissingSandboxDll;
using namespace appbox::test;

namespace
{

/**
 * @brief Build the loader configuration of a case.
 *
 * The configuration carries the filesystem of the sandbox the loader mounts
 * and one startup file which starts the probe process of the case, so a run
 * which starts nothing proves that the loader refused the run.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @param[in] trigger Trigger and marker of the startup file.
 * @return The configuration with one auto start file.
 */
appbox::LoaderConfig BuildConfig(const std::filesystem::path& root, const std::string& trigger)
{
    /* clang-format off */
    auto tree = FsRoot(root, {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"filesystem\\#USERPROFILE#", {}) })
    });
    /* clang-format on */

    auto config = tree.Build();

    appbox::LoaderStartup startup;
    startup.trigger = trigger;
    startup.auto_start = true;
    startup.arguments = { "--startup_marker", trigger };
    config.startups.push_back(std::move(startup));

    return config;
}

/**
 * @brief Remove the injection modules of a case.
 *
 * The harness writes the modules of the run into the resource root of every
 * case, so a case which describes a run without them removes them again.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @param[in] remove32 Whether the 32 bit module is removed.
 * @param[in] remove64 Whether the 64 bit module is removed.
 */
void RemoveSandboxModules(const std::filesystem::path& root, bool remove32, bool remove64)
{
    const std::filesystem::path app(root / appbox::layout::kAppDirNameW);

    std::error_code ec;
    if (remove32)
    {
        std::filesystem::remove(app / appbox::layout::kSandbox32DllNameW, ec);
    }
    if (remove64)
    {
        ec.clear();
        std::filesystem::remove(app / appbox::layout::kSandbox64DllNameW, ec);
    }
}

} // namespace

/**
 * Condition:
 * 1. The resource root of the case carries neither sandbox injection module.
 * 2. The loader runs with a startup file marked for auto start.
 *
 * Expected:
 * 1. The loader refuses the run and reports the module which is missing.
 * 2. No startup file is started, because nothing can be sandboxed.
 */
TEST_F(E2E_Loader_MissingSandboxDll, BothModulesAreMissing)
{
    const auto config = BuildConfig(GetCWD(), "one");
    RemoveSandboxModules(GetCWD(), true, true);

    const auto run = ProbeStartupRun(GetCWDString(), config, "");

    EXPECT_NE(run.exit_code, 0u);
    EXPECT_TRUE(run.started.empty());
}

/**
 * Condition:
 * 1. The resource root of the case carries the 64 bit sandbox injection module
 *    but not the 32 bit one.
 * 2. The loader runs with a startup file marked for auto start.
 *
 * Expected:
 * 1. The loader refuses the run, because a packaged application may start a 32
 *    bit process which has to be injected as well.
 * 2. No startup file is started.
 */
TEST_F(E2E_Loader_MissingSandboxDll, The32BitModuleIsMissing)
{
    const auto config = BuildConfig(GetCWD(), "one");
    RemoveSandboxModules(GetCWD(), true, false);

    const auto run = ProbeStartupRun(GetCWDString(), config, "");

    EXPECT_NE(run.exit_code, 0u);
    EXPECT_TRUE(run.started.empty());
}
