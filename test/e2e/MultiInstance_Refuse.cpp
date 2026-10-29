#include "utils/CommonFixture.hpp"
#include "utils/MultiInstance.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"
#include "utils/TestSandboxId.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

typedef appbox::test::CommonFixture E2E_MultiInstance;
using namespace appbox::test;

namespace
{

/** Text the log of a master instance carries once its own program runs. */
constexpr const char* kApplicationStarted = "the initial application of the sandbox was started";

/** Text the log of a master instance carries once it handled a request. */
constexpr const char* kRequestStarted = "the start request started the process";

/** Budget of a case which waits for a stage of a run, in milliseconds. */
constexpr int kStageTimeoutMs = 60000;

} // namespace

/**
 * Condition:
 * 1. A launcher serves a sandbox and its own program is running.
 * 2. A second launcher carries the identifier of that sandbox but lives in
 *    another root: it is a copy of the archive beside the running one.
 *
 * Expected:
 * 1. The request is refused with `sandbox_mismatch`, so the second launcher
 *    leaves with `ERROR_ACCESS_DENIED` (5).
 * 2. The master instance keeps serving: it exits with zero and started nothing
 *    for the refused request.
 */
TEST_F(E2E_MultiInstance, RefusesDifferentRoot)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto master_config = MultiInstanceConfig(cwd, id, "app");

    StartupRun  master;
    std::thread master_thread([&]() {
        master =
            ProbeStartupRun(GetCWDString(), master_config, "", false, LogRun{}, false, std::wstring(), kMasterHoldMs);
    });

    const auto running = WaitForRunLog(cwd, kApplicationStarted, kStageTimeoutMs);

    /* The second copy of the archive, with the identifier of the running one. */
    const auto      other = cwd / L"other";
    std::error_code ec;
    std::filesystem::create_directories(other, ec);

    std::uint32_t secondary_exit = 0;
    if (running)
    {
        secondary_exit = ProbeRunRawConfig(other.wstring(), MultiInstanceRequestDocument(id), LogRun{});
    }

    master_thread.join();

    ASSERT_TRUE(running) << "the master instance never started its application";
    EXPECT_EQ(secondary_exit, static_cast<std::uint32_t>(ERROR_ACCESS_DENIED));

    /* The refused request started nothing and the master instance kept working. */
    EXPECT_EQ(master.exit_code, 0u);
    EXPECT_EQ(master.reports.size(), static_cast<std::size_t>(1));

    const auto log = ReadRunLog(cwd);
    EXPECT_NE(log.find("sandbox_mismatch"), std::string::npos) << log;
    EXPECT_EQ(log.find(kRequestStarted), std::string::npos) << log;
}

/**
 * Condition:
 * 1. A launcher serves a sandbox and its own program is running.
 * 2. A second launcher carries another identifier, so it belongs to another
 *    sandbox, and lives in a directory of its own.
 *
 * Expected:
 * 1. The second launcher races for the pipe of its own sandbox instead of
 *    joining the running one: it prepares the sandbox of its own directory and
 *    leaves with zero.
 * 2. The two sandboxes are independent: the first one started only its own
 *    program and holds no state of the second one.
 */
TEST_F(E2E_MultiInstance, RefusesDifferentSandboxId)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto master_config = MultiInstanceConfig(cwd, id, "app");

    StartupRun  master;
    std::thread master_thread([&]() {
        master =
            ProbeStartupRun(GetCWDString(), master_config, "", false, LogRun{}, false, std::wstring(), kMasterHoldMs);
    });

    const auto running = WaitForRunLog(cwd, kApplicationStarted, kStageTimeoutMs);

    /*
     * The second sandbox lives in a directory of its own and carries its own
     * identifier, so the second launcher serves it instead of joining the
     * running one. Its startup file runs the probe of the case, which leaves as
     * soon as it reported.
     */
    const auto other = cwd / L"other";
    const auto other_id = SandboxIdForPath(other.wstring());

    auto other_config = MultiInstanceConfig(other, other_id, "other");
    other_config.hide_console = true;
    other_config.startup.screen = false;
    other_config.startups[0].executable = appbox::WideToUTF8(ProbePath(ProbeBitness::Bit64));

    std::uint32_t exit_code = 1;
    if (running)
    {
        exit_code = ProbeRunRawConfig(other.wstring(), nlohmann::json(other_config), LogRun{});
    }

    master_thread.join();

    ASSERT_TRUE(running) << "the master instance never started its application";
    EXPECT_NE(id, other_id);

    /* The second launcher prepared the sandbox of its own directory. */
    EXPECT_EQ(exit_code, 0u);
    EXPECT_TRUE(std::filesystem::exists(other / appbox::layout::kStateDirNameW))
        << "the second launcher did not prepare a sandbox of its own";

    /* The first sandbox ran its own program alone. */
    EXPECT_EQ(master.exit_code, 0u);
    EXPECT_EQ(master.reports.size(), static_cast<std::size_t>(1));

    const auto log = ReadRunLog(cwd);
    EXPECT_EQ(log.find(kRequestStarted), std::string::npos) << log;
}
