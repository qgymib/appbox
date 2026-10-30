#include "utils/CommonFixture.hpp"
#include "utils/MultiInstance.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/RunState.hpp"
#include "utils/SelfExtractingProgram.hpp"
#include "utils/TestSandboxId.hpp"
#include "SandboxId.hpp"
#include "SandboxLayout.hpp"
#include <windows.h>
#include <cstdint>
#include <filesystem>
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
 * 1. A launcher serves a sandbox and its own program is running.
 * 2. A second launcher of the same archive, with the same identifier and the
 *    same root, runs while the program of the first one works.
 *
 * Expected:
 * 1. The second launcher leaves with zero: it handed its request over instead
 *    of preparing a sandbox of its own.
 * 2. The master instance created the program of the request, so the program of
 *    the request reported and names the master instance as the process which
 *    created it, like the program of the run does.
 * 3. The state of the master instance reports the request it handled.
 *
 * The case holds the run of the master instance: the connection of its program
 * is what proves that the sandbox of the run exists, and the case ends the run
 * after the request was answered instead of waiting for a fixed time.
 */
TEST_F(E2E_MultiInstance, JoinsTheRunningSandbox)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto run_config = MultiInstanceConfig(cwd, id, "app");

    StartupRunHandle master(cwd.wstring(), run_config);
    ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
        << "the master instance never started its application";

    const auto& master_reports = master.Collect();
    ASSERT_EQ(master_reports.reports.size(), static_cast<std::size_t>(1)) << master_reports.reports.size();

    /* The request is sent while the program of the master instance works. */
    StartupRun request;
    const auto secondary_exit =
        ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "request", 0, LogRun{}, request);

    /* The run handled the request, which its state counts. */
    appbox::MsgRunState::Rsp state;
    ASSERT_TRUE(QueryRunState(id, state)) << "the state of the run cannot be read";
    EXPECT_EQ(state.handled_requests, static_cast<std::uint64_t>(1));

    master.Exit(0);
    const auto master_exit = master.Wait();

    EXPECT_EQ(secondary_exit, 0u);
    EXPECT_EQ(master_exit, 0u);

    /* The master instance started its own program and the program of the request. */
    ASSERT_EQ(request.reports.size(), static_cast<std::size_t>(1)) << request.reports.size();

    EXPECT_EQ(master_reports.reports[0].parent_pid, master_reports.launcher_pid)
        << "the program of the run is not its own";
    EXPECT_EQ(request.reports[0].parent_pid, master_reports.launcher_pid)
        << "the program of the request is not its own";
    EXPECT_NE(master_reports.reports[0].pid, request.reports[0].pid);
}

/**
 * Condition:
 * 1. A launcher serves a sandbox and is still preparing it: its control pipe
 *    exists, its program was not started yet.
 * 2. A second launcher of the same archive sends its request in that window.
 *
 * Expected:
 * 1. The request is queued instead of being refused, so the second launcher
 *    waits for the answer instead of failing.
 * 2. The master instance handles the request once its own program started and
 *    the second launcher leaves with zero.
 * 3. The state of the master instance counts the request it queued and the one
 *    request it handled.
 *
 * The case holds the run of the master instance and sends its request before it
 * waits for the program of that run, so the request arrives while the master
 * instance is still preparing its sandbox or while it starts its program. The
 * case does not pin which of the two it was: the preparation of the sandbox and
 * the start of the client take the same order of time on a machine the case does
 * not control, so a case which required the request to arrive before the
 * preparation would pass or fail for the speed of the machine. The state of the
 * run reports both counters, so a reader of a failed artifact sees which of the
 * two happened.
 */
TEST_F(E2E_MultiInstance, QueuesRequestDuringPreparation)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto run_config = MultiInstanceConfig(cwd, id, "app");

    StartupRunHandle master(cwd.wstring(), run_config);

    /*
     * The pipe exists from the moment the master instance won the race for it,
     * which is before it extracted its payload and mounted its layers.
     */
    const auto serving = WaitForControlPipe(id, static_cast<int>(kStageTimeoutMs));
    ASSERT_TRUE(serving) << "the master instance never opened its control pipe";

    StartupRun request;
    const auto secondary_exit =
        ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "request", 0, LogRun{}, request);

    /* The queued request was handled after the initial target started. */
    ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
        << "the master instance never started its application";

    const auto& master_reports = master.Collect();
    ASSERT_EQ(master_reports.reports.size(), static_cast<std::size_t>(1)) << master_reports.reports.size();

    /*
     * The request waited for the control thread instead of being refused, which
     * the state of the run counts: the master instance handled it once its
     * sandbox existed. The state reports as well how many requests arrived
     * before the sandbox was prepared, which the case does not pin, because the
     * window depends on the speed of the machine (see the comment above).
     */
    appbox::MsgRunState::Rsp state;
    ASSERT_TRUE(QueryRunState(id, state)) << "the state of the run cannot be read";
    EXPECT_EQ(state.queued_requests, static_cast<std::uint64_t>(1));
    EXPECT_LE(state.queued_before_prepared, state.queued_requests);
    EXPECT_EQ(state.handled_requests, static_cast<std::uint64_t>(1));

    master.Exit(0);
    const auto master_exit = master.Wait();

    EXPECT_EQ(secondary_exit, 0u);
    EXPECT_EQ(master_exit, 0u);

    ASSERT_EQ(request.reports.size(), static_cast<std::size_t>(1)) << request.reports.size();
}

/**
 * Condition:
 * 1. A launcher serves a sandbox and its own program is running.
 * 2. A second launcher of the same archive runs without any log option.
 *
 * Expected:
 * 1. The second launcher leaves with zero, so the handshake took place.
 * 2. The state directory of the sandbox holds the same entries with the same
 *    sizes and write times: the second instance wrote nothing into it.
 * 3. The log directory of the case holds the same number of files: the second
 *    instance wrote no log of its own.
 * 4. No extraction folder appears beside the program: the second instance
 *    extracted no payload.
 */
TEST_F(E2E_MultiInstance, SecondInstanceWritesNoState)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto run_config = MultiInstanceConfig(cwd, id, "app");

    StartupRunHandle master(cwd.wstring(), run_config);
    ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
        << "the master instance never started its application";

    /*
     * The state of the sandbox is compared once its own process initialized the
     * sandbox: the report of that program is what proves that it runs inside
     * the sandbox, because mounting the hive and writing the files of the
     * process are the work of that process and not of the second instance.
     */
    const auto& run_reports = master.Collect();
    ASSERT_EQ(run_reports.reports.size(), static_cast<std::size_t>(1)) << run_reports.reports.size();

    /* The state of the sandbox and the logs of the case before the second run. */
    const auto state = TreeSnapshot(cwd / appbox::layout::kStateDirNameW);
    const auto logs = LogFileCount(cwd);

    LogRun silent;
    silent.pass_level = false;
    silent.pass_dir = false;

    const auto secondary_exit =
        ProbeRunRawConfig(GetCWDString(), MultiInstanceRequestDocument(id), silent, kSecondaryConfigName);

    const auto state_after = TreeSnapshot(cwd / appbox::layout::kStateDirNameW);
    const auto logs_after = LogFileCount(cwd);

    master.Exit(0);
    const auto master_exit = master.Wait();

    EXPECT_EQ(secondary_exit, 0u);
    EXPECT_EQ(master_exit, 0u);

    EXPECT_EQ(state_after, state) << "the second instance wrote into the state of the sandbox";
    EXPECT_EQ(logs_after, logs) << "the second instance wrote a log file";
    EXPECT_FALSE(std::filesystem::exists(cwd / L"import")) << "the second instance extracted a payload";
}

/**
 * Condition:
 * 1. A sandbox runs from a configuration file beside its launcher.
 * 2. A sandbox runs from a self-extracting program, whose root is the directory
 *    of the program itself.
 * 3. In both layouts a second launcher of the same sandbox sends its request.
 *
 * Expected:
 * 1. The request is accepted in both layouts: the second launcher leaves with
 *    zero and the master instance started the program of the request.
 * 2. The root of a self-extracting sandbox is the directory of the program and
 *    not the folder the payload is extracted into, so the second launcher of
 *    that directory joins the sandbox.
 */
TEST_F(E2E_MultiInstance, ConfigFileAndSelfExtracting)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());

    /* The layout of a configuration file: the root is the directory of the file. */
    {
        const auto run_config = MultiInstanceConfig(cwd, id, "app");

        StartupRunHandle master(cwd.wstring(), run_config);
        ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
            << "the master instance of the configuration layout never started";

        const auto& master_reports = master.Collect();
        ASSERT_EQ(master_reports.reports.size(), static_cast<std::size_t>(1)) << master_reports.reports.size();

        StartupRun request;
        const auto exit_code =
            ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "request", 0, LogRun{}, request);

        master.Exit(0);
        const auto master_exit = master.Wait();

        EXPECT_EQ(exit_code, 0u);
        EXPECT_EQ(master_exit, 0u);
        EXPECT_EQ(request.reports.size(), static_cast<std::size_t>(1));
    }

    /* The layout of a self-extracting program: the root is its own directory. */
    {
        const auto root = cwd / L"program";
        ASSERT_TRUE(std::filesystem::create_directories(root));

        std::string error;
        std::string program_id;
        ASSERT_TRUE(PackProbeSelfExtracting(root, L"app", appbox::StartupScreen{ false }, error, &program_id)) << error;
        ASSERT_TRUE(appbox::IsValidSandboxId(program_id)) << program_id;

        const auto program = root / kSelfExtractingProgramName;
        ASSERT_TRUE(std::filesystem::is_regular_file(program));

        /* The run reads the configuration of the archive in the image of the
         * program itself, which is what a packed archive does. */
        StartupRunHandle master(root.wstring(), program.wstring(), "", "app", LogRun{});
        ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
            << "the master instance of the self-extracting layout never started";

        const auto& master_reports = master.Collect();
        ASSERT_EQ(master_reports.reports.size(), static_cast<std::size_t>(1)) << master_reports.reports.size();

        StartupRun request;
        /*
         * The second instance of a self-extracting archive is the archive
         * itself: the client runs the program of the master instance, which is
         * what makes the request a request of the same sandbox.
         */
        const auto exit_code =
            ProbeRunSecondary(root.wstring(), MultiInstanceRequestDocument(program_id), "", "request", 0, LogRun{},
                              request, std::wstring(), nlohmann::json::object(), program.wstring());

        master.Exit(0);
        const auto master_exit = master.Wait();

        EXPECT_EQ(exit_code, 0u);
        EXPECT_EQ(master_exit, 0u);
        EXPECT_EQ(request.reports.size(), static_cast<std::size_t>(1));
    }
}
