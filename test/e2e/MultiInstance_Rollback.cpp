#include "utils/CommonFixture.hpp"
#include "utils/MultiInstance.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"
#include "utils/RunState.hpp"
#include "utils/TestSandboxId.hpp"
#include "WString.hpp"
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

typedef appbox::test::CommonFixture E2E_MultiInstance;
using namespace appbox::test;

namespace
{

/** Budget of a case which waits for the program of a run, in milliseconds. */
constexpr std::uint32_t kStageTimeoutMs = 60000;

/** Exit code the program of a request reports in the case of the exit code. */
constexpr std::uint32_t kRequestExitCode = 7;

/** Name of the file of the case which is no program at all. */
constexpr const wchar_t* kBrokenProgramName = L"appbox_broken_program.exe";

/**
 * @brief Write a file which carries no image of a program.
 *
 * The launcher resolves the path of a startup file before it creates the
 * process, so a file which does not exist is refused before anything is
 * created. A file which exists and is no image passes the resolution and fails
 * the creation, which is the failure a rollback of a request has to cover.
 *
 * @param[in] path Path of the file to write.
 * @return true on success.
 */
bool WriteBrokenProgram(const std::filesystem::path& path)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return false;
    }

    stream.write("no program", 10);
    return stream.good();
}

} // namespace

/**
 * Condition:
 * 1. The configuration lists two programs for an auto start, the second of them
 *    names a file which exists and is no program at all.
 * 2. A master instance runs its first program, which is the probe of the case.
 * 3. A second launcher asks for the auto start files, so the request builds both
 *    programs.
 *
 * Expected:
 * 1. The request is refused: the second launcher leaves with the code of the
 *    failed creation.
 * 2. The program the request already started is terminated, so the run carries
 *    no half of a request. The state of the run reports that program as a
 *    program of the request which the rollback terminated.
 * 3. The request reports no program, because a program the master instance
 *    terminated has nothing to report and the case must not fail for it.
 * 4. The program of the run keeps working and decides the exit code of the run,
 *    which is the one of the initial target.
 *
 * The case holds the run of the master instance, so the program of the run
 * cannot leave on its own: its answer after the rolled back request is what
 * proves that the rollback touched the programs of the request only. The state
 * of the run is read after the second launcher left, which is after the master
 * instance answered it, which is after the rollback: the case therefore needs no
 * wait to observe the terminated program.
 */
TEST_F(E2E_MultiInstance, AutoRollback)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());

    auto launcher_config = MultiInstanceConfig(cwd, id, "app");

    /*
     * The case starts the launcher with the configured executables, so the first
     * startup file is the probe of the case and the second one names a file
     * which exists and is no program.
     */
    launcher_config.startups[0].executable = appbox::WideToUTF8(ProbePath(ProbeBitness::Bit64));

    const auto broken = cwd / kBrokenProgramName;
    ASSERT_TRUE(WriteBrokenProgram(broken));

    appbox::LauncherStartup missing;
    missing.trigger = "missing";
    missing.auto_start = true;
    missing.executable = appbox::WideToUTF8(broken.wstring());
    launcher_config.startups.push_back(missing);

    /* The run starts the program which works, not the file which is missing. */
    StartupRunHandle master(cwd.wstring(), launcher_config, "app", false, LogRun{}, true);
    ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
        << "the master instance never started its application";

    const auto& master_reports = master.Collect();
    ASSERT_EQ(master_reports.reports.size(), static_cast<std::size_t>(1)) << master_reports.reports.size();

    StartupRun request;
    const auto secondary_exit =
        ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "request", 0, LogRun{}, request);

    /*
     * The request is refused with the code of the failed creation. The exact
     * code is the reason the loader reports for a file which is no image and
     * differs between the versions of the system, so the case pins that the
     * request failed at all; the state below pins the rest.
     */
    EXPECT_NE(secondary_exit, 0u);

    /*
     * The master instance created the first program of the request and rolled it
     * back when the second one could not be created: the state of the run
     * reports the program of the request as terminated and counts it.
     */
    appbox::MsgRunState::Rsp state;
    ASSERT_TRUE(QueryRunState(id, state)) << "the state of the run cannot be read";

    const auto request_programs = RequestPrograms(state);
    ASSERT_EQ(request_programs.size(), static_cast<std::size_t>(1)) << state.programs.size();
    EXPECT_TRUE(request_programs[0].terminated) << "the rollback left the program of the request";
    EXPECT_EQ(state.rolled_back_processes, static_cast<std::uint64_t>(1));
    EXPECT_EQ(state.handled_requests, static_cast<std::uint64_t>(0));

    /* The program of the run is the only program of the run which still works. */
    EXPECT_EQ(RunningPrograms(state), static_cast<std::size_t>(1)) << "the run kept more than its own program";

    /*
     * A refused request reports no program: the master instance terminated the
     * one it already started, so nothing was left which could answer the case.
     */
    EXPECT_TRUE(request.reports.empty()) << request.reports.size();

    /*
     * The rollback terminated the program of the request only: the program of
     * the run still answers the case and leaves with zero, which is the exit
     * code of the run.
     */
    EXPECT_EQ(master.Ping(), static_cast<std::size_t>(1)) << "the rollback ended the program of the run";

    master.Exit(0);
    EXPECT_EQ(master.Wait(), 0u);
}

/**
 * Condition:
 * 1. A master instance runs its own program, which leaves with zero.
 * 2. A second launcher asks for a program which leaves with a code of its own.
 *
 * Expected:
 * 1. The request is accepted and its program really leaves with its own code,
 *    which the state of the run reports for that program.
 * 2. The exit code of the run is the one of the initial target: a program a
 *    request started is the work of another launcher and does not decide the
 *    exit code of this run.
 */
TEST_F(E2E_MultiInstance, ExitCodeIsolation)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto launcher_config = MultiInstanceConfig(cwd, id, "app");

    StartupRunHandle master(cwd.wstring(), launcher_config);
    ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
        << "the master instance never started its application";

    const auto& master_reports = master.Collect();
    ASSERT_EQ(master_reports.reports.size(), static_cast<std::size_t>(1)) << master_reports.reports.size();

    StartupRun request;
    const auto secondary_exit = ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "request",
                                                  kRequestExitCode, LogRun{}, request);

    /* The program of the request left, the program of the run did not. */
    EXPECT_EQ(master.Ping(), static_cast<std::size_t>(1)) << "the request ended the program of the run";

    /*
     * The exit code of a program of a request is a fact of the run as well. The
     * case waits for the report of the end of that program instead of assuming
     * that the master instance reaped it already: the wait returns as soon as
     * the state reports it and needs no fixed delay.
     */
    RunStateClient state_client(id);
    ASSERT_TRUE(state_client.IsOpen()) << "the control pipe of the sandbox cannot be opened";

    appbox::MsgRunState::Rsp state;
    ASSERT_TRUE(state_client.WaitFor(
        [](const appbox::MsgRunState::Rsp& current) {
            for (const auto& program : RequestPrograms(current))
            {
                if (program.exited)
                {
                    return true;
                }
            }

            return false;
        },
        state))
        << "the program of the request did not leave";

    const auto request_programs = RequestPrograms(state);
    ASSERT_EQ(request_programs.size(), static_cast<std::size_t>(1)) << state.programs.size();
    EXPECT_EQ(request_programs[0].exit_code, kRequestExitCode) << "the program of the request left with another code";
    EXPECT_FALSE(request_programs[0].terminated);

    /* The handshake itself succeeded, the program of the request ran and left
     * with its own code. */
    EXPECT_EQ(secondary_exit, 0u);
    ASSERT_EQ(request.reports.size(), static_cast<std::size_t>(1)) << request.reports.size();

    /* The run reports the code of its own program, which left with zero. */
    master.Exit(0);
    EXPECT_EQ(master.Wait(), 0u);
}
