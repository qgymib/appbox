#include "utils/CommonFixture.hpp"
#include "utils/MultiInstance.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"
#include "utils/SelfExtractingProgram.hpp"
#include "utils/TestSandboxId.hpp"
#include "SandboxId.hpp"
#include "SandboxLayout.hpp"
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

/**
 * @brief Text the log of a master instance carries once its own program runs.
 *
 * The line is the last stage of the preparation of a run, so a case which waits
 * for it knows that the sandbox exists and that the program of the run works.
 */
constexpr const char* kApplicationStarted = "the initial application of the sandbox was started";

/** Text the log of a master instance carries once it handled a request. */
constexpr const char* kRequestStarted = "the start request started the process";

/** Text the log of a master instance carries once a request is queued. */
constexpr const char* kRequestQueued = "is queued";

/**
 * @brief Text the log of a sandboxed process carries once its sandbox is ready.
 *
 * The line is written after the module table of the process was initialized, so
 * a case which waits for it knows that the sandbox of the process mounted its
 * hive and wrote the entries of its state.
 */
constexpr const char* kSandboxInitialized = "AppBox Sandbox initialized for ";

/** Budget of a case which waits for a stage of a run, in milliseconds. */
constexpr int kStageTimeoutMs = 60000;

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
 * 3. The master instance reports the request it handled in its log.
 */
TEST_F(E2E_MultiInstance, JoinsTheRunningSandbox)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto run_config = MultiInstanceConfig(cwd, id, "app");

    StartupRun  master;
    std::thread master_thread([&]() {
        master = ProbeStartupRun(GetCWDString(), run_config, "", false, LogRun{}, false, std::wstring(), kMasterHoldMs);
    });

    /*
     * The request is sent while the program of the master instance works: the
     * log line proves that the sandbox of the run is prepared and that its
     * application is running.
     */
    const auto running = WaitForRunLog(cwd, kApplicationStarted, kStageTimeoutMs);

    StartupRun    request;
    std::uint32_t secondary_exit = 1;
    if (running)
    {
        secondary_exit =
            ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "request", 0, LogRun{}, request);
    }

    master_thread.join();

    ASSERT_TRUE(running) << "the master instance never started its application";
    EXPECT_EQ(secondary_exit, 0u);
    EXPECT_EQ(master.exit_code, 0u);

    /* The master instance started its own program and the program of the request. */
    ASSERT_EQ(master.reports.size(), static_cast<std::size_t>(1)) << master.reports.size();
    ASSERT_EQ(request.reports.size(), static_cast<std::size_t>(1)) << request.reports.size();

    EXPECT_EQ(master.reports[0].parent_pid, master.launcher_pid) << "the program of the run is not its own";
    EXPECT_EQ(request.reports[0].parent_pid, master.launcher_pid) << "the program of the request is not its own";
    EXPECT_NE(master.reports[0].pid, request.reports[0].pid);

    const auto log = ReadRunLog(cwd);
    EXPECT_NE(log.find(kRequestStarted), std::string::npos) << log;
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
 * 3. The log of the master instance names both the queued request and the
 *    process it started.
 */
TEST_F(E2E_MultiInstance, QueuesRequestDuringPreparation)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto run_config = MultiInstanceConfig(cwd, id, "app");

    StartupRun  master;
    std::thread master_thread([&]() {
        master = ProbeStartupRun(GetCWDString(), run_config, "", false, LogRun{}, false, std::wstring(), kMasterHoldMs);
    });

    /*
     * The pipe exists from the moment the master instance won the race for it,
     * which is before it extracted its payload and mounted its layers.
     */
    const auto serving = WaitForControlPipe(id, kStageTimeoutMs);

    StartupRun    request;
    std::uint32_t secondary_exit = 1;
    if (serving)
    {
        secondary_exit =
            ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "request", 0, LogRun{}, request);
    }

    master_thread.join();

    ASSERT_TRUE(serving) << "the master instance never opened its control pipe";
    EXPECT_EQ(secondary_exit, 0u);
    EXPECT_EQ(master.exit_code, 0u);

    ASSERT_EQ(master.reports.size(), static_cast<std::size_t>(1)) << master.reports.size();
    ASSERT_EQ(request.reports.size(), static_cast<std::size_t>(1)) << request.reports.size();

    const auto log = ReadRunLog(cwd);
    EXPECT_NE(log.find(kRequestQueued), std::string::npos) << log;
    EXPECT_NE(log.find(kRequestStarted), std::string::npos) << log;
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

    StartupRun  master;
    std::thread master_thread([&]() {
        master = ProbeStartupRun(GetCWDString(), run_config, "", false, LogRun{}, false, std::wstring(), kMasterHoldMs);
    });

    /*
     * The state of the sandbox is compared once its own process initialized the
     * sandbox: mounting the hive and writing the files of the process are the
     * work of that process and not of the second instance.
     */
    const auto running = WaitForRunLog(cwd, kSandboxInitialized, kStageTimeoutMs);

    /* The state of the sandbox and the logs of the case before the second run. */
    const auto state = TreeSnapshot(cwd / appbox::layout::kStateDirNameW);
    const auto logs = LogFileCount(cwd);

    LogRun silent;
    silent.pass_level = false;
    silent.pass_dir = false;

    std::uint32_t secondary_exit = 1;
    if (running)
    {
        secondary_exit =
            ProbeRunRawConfig(GetCWDString(), MultiInstanceRequestDocument(id), silent, kSecondaryConfigName);
    }

    const auto state_after = TreeSnapshot(cwd / appbox::layout::kStateDirNameW);
    const auto logs_after = LogFileCount(cwd);

    master_thread.join();

    ASSERT_TRUE(running) << "the sandbox of the master instance never initialized";
    EXPECT_EQ(secondary_exit, 0u);

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

        StartupRun  master;
        std::thread master_thread([&]() {
            master =
                ProbeStartupRun(GetCWDString(), run_config, "", false, LogRun{}, false, std::wstring(), kMasterHoldMs);
        });

        const auto running = WaitForRunLog(cwd, kApplicationStarted, kStageTimeoutMs);

        StartupRun    request;
        std::uint32_t exit_code = 1;
        if (running)
        {
            exit_code =
                ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "request", 0, LogRun{}, request);
        }

        master_thread.join();

        ASSERT_TRUE(running) << "the master instance of the configuration layout never started";
        EXPECT_EQ(exit_code, 0u);
        EXPECT_EQ(master.exit_code, 0u);
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

        StartupRun  master;
        std::thread master_thread([&]() {
            master = ProbeStartupRunEmbedded(root.wstring(), program.wstring(), "", "app", LogRun{}, kMasterHoldMs);
        });

        const auto running = WaitForRunLog(root, kApplicationStarted, kStageTimeoutMs);

        StartupRun    request;
        std::uint32_t exit_code = 1;
        if (running)
        {
            /*
             * The second instance of a self-extracting archive is the archive
             * itself: the client runs the program of the master instance, which
             * is what makes the request a request of the same sandbox.
             */
            exit_code =
                ProbeRunSecondary(root.wstring(), MultiInstanceRequestDocument(program_id), "", "request", 0, LogRun{},
                                  request, std::wstring(), nlohmann::json::object(), program.wstring());
        }

        master_thread.join();

        ASSERT_TRUE(running) << "the master instance of the self-extracting layout never started";
        EXPECT_EQ(exit_code, 0u);
        EXPECT_EQ(master.exit_code, 0u);
        EXPECT_EQ(request.reports.size(), static_cast<std::size_t>(1));
    }
}
