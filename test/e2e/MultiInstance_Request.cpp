#include "utils/CommonFixture.hpp"
#include "utils/MultiInstance.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/RunState.hpp"
#include "utils/TestSandboxId.hpp"
#include <cstdint>
#include <string>

typedef appbox::test::CommonFixture E2E_MultiInstance;
using namespace appbox::test;

namespace
{

/** Budget of a case which waits for the program of a run, in milliseconds. */
constexpr std::uint32_t kStageTimeoutMs = 60000;

} // namespace

/**
 * Condition:
 * 1. A launcher serves a sandbox and its own program works.
 * 2. A second launcher of the same archive sends its request while that program
 *    works.
 *
 * Expected:
 * 1. The request is handled at once instead of being queued until the program
 *    of the run exits: the program of the run still answers the case after the
 *    request was answered, which a program which left cannot do.
 * 2. The master instance started the program of the request and kept its own
 *    program running.
 * 3. Both programs name the master instance as the process which created them.
 *
 * The case names no budget of its own: it holds the run of the master instance
 * and ends that run after the request was answered, so the program of the run
 * cannot leave on its own. A master instance which handled the request only at
 * the end of its run would never answer it, and the second launcher would leave
 * with the code of its own request budget instead of zero.
 */
TEST_F(E2E_MultiInstance, RequestWhileRunning)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto launcher_config = MultiInstanceConfig(cwd, id, "app");

    /*
     * The case holds the run of the master instance: its program reports to the
     * case and stays alive until the case ends it, so the run serves the request
     * below and leaves as soon as the case asks it to.
     */
    StartupRunHandle master(cwd.wstring(), launcher_config);
    ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
        << "the master instance never started its application";

    const auto& master_reports = master.Collect();
    ASSERT_EQ(master_reports.reports.size(), static_cast<std::size_t>(1)) << master_reports.reports.size();

    /* The request is sent while the program of the master instance works. */
    StartupRun request;
    const auto secondary_exit =
        ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "request", 0, LogRun{}, request);

    /*
     * The program of the run answered the case again after the request was
     * answered, so the master instance served the request while that program
     * worked instead of waiting for it to exit.
     */
    EXPECT_EQ(master.Ping(), static_cast<std::size_t>(1)) << "the request waited for the program of the run to exit";

    /*
     * The run handled the request: the state of the run counts the request
     * whose programs it created, which is what the log of the run used to
     * report.
     */
    appbox::MsgRunState::Rsp state;
    ASSERT_TRUE(QueryRunState(id, state)) << "the state of the run cannot be read";
    EXPECT_EQ(state.handled_requests, static_cast<std::uint64_t>(1));

    master.Exit(0);
    const auto master_exit = master.Wait();

    EXPECT_EQ(secondary_exit, 0u);
    EXPECT_EQ(master_exit, 0u);

    /* The program of the run and the program of the request were created by the
     * master instance, and they are two different processes. */
    ASSERT_EQ(request.reports.size(), static_cast<std::size_t>(1)) << request.reports.size();

    EXPECT_EQ(master_reports.reports[0].parent_pid, master_reports.launcher_pid)
        << "the program of the run is not its own";
    EXPECT_EQ(request.reports[0].parent_pid, master_reports.launcher_pid)
        << "the program of the request is not its own";
    EXPECT_NE(master_reports.reports[0].pid, request.reports[0].pid);
}

/**
 * Condition:
 * 1. A launcher serves a sandbox and its own program works.
 * 2. Two second launchers of the same sandbox send a request one after the
 *    other, each with a marker of its own.
 *
 * Expected:
 * 1. Every request starts exactly one program.
 * 2. The command line of a program carries the marker of its own request and
 *    none of the other request: the arguments of a request are a copy which is
 *    appended to the arguments of the startup file and never reach the
 *    configuration or another request.
 * 3. The run of the master instance serves both requests, so it still works
 *    after the second one and leaves with zero when the case ends it.
 */
TEST_F(E2E_MultiInstance, ArgsIsolation)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto launcher_config = MultiInstanceConfig(cwd, id, "app");

    StartupRunHandle master(cwd.wstring(), launcher_config);
    ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
        << "the master instance never started its application";

    const auto& master_reports = master.Collect();
    ASSERT_EQ(master_reports.reports.size(), static_cast<std::size_t>(1)) << master_reports.reports.size();

    StartupRun first;
    StartupRun second;
    const auto first_exit =
        ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "first", 0, LogRun{}, first);
    const auto second_exit =
        ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "second", 0, LogRun{}, second);

    /* The run served both requests, so its own program is still the one of the
     * case and the requests changed nothing about it. */
    EXPECT_EQ(master.Ping(), static_cast<std::size_t>(1));

    master.Exit(0);
    const auto master_exit = master.Wait();

    EXPECT_EQ(first_exit, 0u);
    EXPECT_EQ(second_exit, 0u);
    EXPECT_EQ(master_exit, 0u);

    ASSERT_EQ(first.reports.size(), static_cast<std::size_t>(1)) << first.reports.size();
    ASSERT_EQ(second.reports.size(), static_cast<std::size_t>(1)) << second.reports.size();

    const auto& first_line = first.reports[0].command_line;
    const auto& second_line = second.reports[0].command_line;

    EXPECT_NE(first_line.find("first"), std::string::npos) << first_line;
    EXPECT_EQ(first_line.find("second"), std::string::npos) << first_line;

    EXPECT_NE(second_line.find("second"), std::string::npos) << second_line;
    EXPECT_EQ(second_line.find("first"), std::string::npos) << second_line;
}
