#include "utils/CommonFixture.hpp"
#include "utils/MultiInstance.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/TestSandboxId.hpp"
#include <windows.h>
#include <cstdint>
#include <string>
#include <thread>

typedef appbox::test::CommonFixture E2E_MultiInstance;
using namespace appbox::test;

namespace
{

/** Text the log of a master instance carries once its own program runs. */
constexpr const char* kApplicationStarted = "the initial application of the sandbox was started";

/** Text the log of a master instance carries once it started a program of a request. */
constexpr const char* kRequestStarted = "the start request started the process";

/** Budget of a case which waits for a stage of a run, in milliseconds. */
constexpr int kStageTimeoutMs = 60000;

/**
 * @brief Time the initial program of a master instance stays alive, in milliseconds.
 *
 * The program outlives every request of a case, so a case which measures how
 * long a request waits tells the handling of the control loop apart from the end
 * of the run.
 */
constexpr std::uint32_t kRunHoldMs = 6000;

/**
 * @brief Budget a request has to be handled in while the program of the run works.
 *
 * The budget is the acceptance threshold of the multiple instances of
 * `docs/Launcher.md`: a request which is only handled when the program of the
 * run exits needs at least the time that program stays alive, which is far
 * above the budget.
 */
constexpr ULONGLONG kRequestBudgetMs = 2000;

} // namespace

/**
 * Condition:
 * 1. A launcher serves a sandbox and its own program works for a while.
 * 2. A second launcher of the same archive sends its request while that program
 *    works.
 *
 * Expected:
 * 1. The request is handled at once instead of being queued until the program
 *    of the run exits: the second launcher leaves within the acceptance
 *    threshold, which is far below the time the program of the run stays alive.
 * 2. The master instance started the program of the request and kept its own
 *    program running.
 * 3. Both programs name the master instance as the process which created them.
 */
TEST_F(E2E_MultiInstance, RequestWhileRunning)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto launcher_config = MultiInstanceConfig(cwd, id, "app");

    StartupRun  master;
    std::thread master_thread([&]() {
        master =
            ProbeStartupRun(GetCWDString(), launcher_config, "", false, LogRun{}, false, std::wstring(), kRunHoldMs);
    });

    /* The request is sent while the program of the master instance works. */
    const auto running = WaitForRunLog(cwd, kApplicationStarted, kStageTimeoutMs);

    StartupRun    request;
    std::uint32_t secondary_exit = 1;
    ULONGLONG     elapsed = 0;
    if (running)
    {
        const ULONGLONG started = GetTickCount64();
        secondary_exit =
            ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "request", 0, LogRun{}, request);
        elapsed = GetTickCount64() - started;
    }

    master_thread.join();

    ASSERT_TRUE(running) << "the master instance never started its application";
    EXPECT_EQ(secondary_exit, 0u);
    EXPECT_EQ(master.exit_code, 0u);

    /* The request was handled while the program of the run was still working. */
    EXPECT_LT(elapsed, kRequestBudgetMs) << "the request waited for the program of the run to exit";
    EXPECT_NE(ReadRunLog(cwd).find(kRequestStarted), std::string::npos);

    /* The program of the run and the program of the request were created by the
     * master instance, and they are two different processes. */
    ASSERT_EQ(master.reports.size(), static_cast<std::size_t>(1)) << master.reports.size();
    ASSERT_EQ(request.reports.size(), static_cast<std::size_t>(1)) << request.reports.size();

    EXPECT_EQ(master.reports[0].parent_pid, master.launcher_pid) << "the program of the run is not its own";
    EXPECT_EQ(request.reports[0].parent_pid, master.launcher_pid) << "the program of the request is not its own";
    EXPECT_NE(master.reports[0].pid, request.reports[0].pid);
}

/**
 * Condition:
 * 1. A launcher serves a sandbox and its own program works for a while.
 * 2. Two second launchers of the same sandbox send a request one after the
 *    other, each with a marker of its own.
 *
 * Expected:
 * 1. Every request starts exactly one program.
 * 2. The command line of a program carries the marker of its own request and
 *    none of the other request: the arguments of a request are a copy which is
 *    appended to the arguments of the startup file and never reach the
 *    configuration or another request.
 */
TEST_F(E2E_MultiInstance, ArgsIsolation)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto launcher_config = MultiInstanceConfig(cwd, id, "app");

    StartupRun  master;
    std::thread master_thread([&]() {
        master =
            ProbeStartupRun(GetCWDString(), launcher_config, "", false, LogRun{}, false, std::wstring(), kRunHoldMs);
    });

    const auto running = WaitForRunLog(cwd, kApplicationStarted, kStageTimeoutMs);

    StartupRun    first;
    StartupRun    second;
    std::uint32_t first_exit = 1;
    std::uint32_t second_exit = 1;
    if (running)
    {
        first_exit =
            ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "first", 0, LogRun{}, first);
        second_exit =
            ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "second", 0, LogRun{}, second);
    }

    master_thread.join();

    ASSERT_TRUE(running) << "the master instance never started its application";
    EXPECT_EQ(first_exit, 0u);
    EXPECT_EQ(second_exit, 0u);
    EXPECT_EQ(master.exit_code, 0u);

    ASSERT_EQ(first.reports.size(), static_cast<std::size_t>(1)) << first.reports.size();
    ASSERT_EQ(second.reports.size(), static_cast<std::size_t>(1)) << second.reports.size();

    const auto& first_line = first.reports[0].command_line;
    const auto& second_line = second.reports[0].command_line;

    EXPECT_NE(first_line.find("first"), std::string::npos) << first_line;
    EXPECT_EQ(first_line.find("second"), std::string::npos) << first_line;

    EXPECT_NE(second_line.find("second"), std::string::npos) << second_line;
    EXPECT_EQ(second_line.find("first"), std::string::npos) << second_line;
}
