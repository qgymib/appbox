#include "utils/CommonFixture.hpp"
#include "utils/MultiInstance.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"
#include "utils/TestSandboxId.hpp"
#include "WString.hpp"
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

typedef appbox::test::CommonFixture E2E_MultiInstance;
using namespace appbox::test;

namespace
{

/** Text the log of a master instance carries once its own program runs. */
constexpr const char* kApplicationStarted = "the initial application of the sandbox was started";

/** Text the log of a master instance carries once it rolled a request back. */
constexpr const char* kRequestRolledBack = "process(es) of the request terminated";

/** Budget of a case which waits for a stage of a run, in milliseconds. */
constexpr int kStageTimeoutMs = 60000;

/** Time the initial program of a master instance stays alive, in milliseconds. */
constexpr std::uint32_t kRunHoldMs = 6000;

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
 *    no half of a request.
 * 3. The program of the run keeps working and decides the exit code of the run,
 *    which is the one of the initial target.
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

    StartupRun  master;
    std::thread master_thread([&]() {
        /* The run starts the program which works, not the file which is missing. */
        master =
            ProbeStartupRun(GetCWDString(), launcher_config, "app", false, LogRun{}, true, std::wstring(), kRunHoldMs);
    });

    const auto running = WaitForRunLog(cwd, kApplicationStarted, kStageTimeoutMs);

    StartupRun    request;
    std::uint32_t secondary_exit = 0;
    if (running)
    {
        secondary_exit =
            ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "request", 0, LogRun{}, request);
    }

    master_thread.join();

    ASSERT_TRUE(running) << "the master instance never started its application";

    /*
     * The request is refused with the code of the failed creation. The exact
     * code is the reason the loader reports for a file which is no image and
     * differs between the versions of the system, so the case pins that the
     * request failed at all; the rollback below pins the rest.
     */
    EXPECT_NE(secondary_exit, 0u);

    const auto log = ReadRunLog(cwd);
    EXPECT_NE(log.find(kRequestRolledBack), std::string::npos) << log;

    /* The rollback terminated the program of the request only: the program of
     * the run worked and left with zero, which is the exit code of the run. */
    EXPECT_EQ(master.exit_code, 0u);
}

/**
 * Condition:
 * 1. A master instance runs its own program, which leaves with zero.
 * 2. A second launcher asks for a program which leaves with a code of its own.
 *
 * Expected:
 * 1. The request is accepted and its program really leaves with its own code,
 *    which the log of the run reports.
 * 2. The exit code of the run is the one of the initial target: a program a
 *    request started is the work of another launcher and does not decide the
 *    exit code of this run.
 */
TEST_F(E2E_MultiInstance, ExitCodeIsolation)
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

    StartupRun    request;
    std::uint32_t secondary_exit = 1;
    if (running)
    {
        secondary_exit = ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "request",
                                           kRequestExitCode, LogRun{}, request);
    }

    master_thread.join();

    ASSERT_TRUE(running) << "the master instance never started its application";

    /* The handshake itself succeeded, the program of the request ran and left
     * with its own code. */
    EXPECT_EQ(secondary_exit, 0u);
    ASSERT_EQ(request.reports.size(), static_cast<std::size_t>(1)) << request.reports.size();

    const auto log = ReadRunLog(cwd);
    EXPECT_NE(log.find("application exited with code " + std::to_string(kRequestExitCode)), std::string::npos) << log;

    /* The run reports the code of its own program, which left with zero. */
    EXPECT_EQ(master.exit_code, 0u);
}
