#include "utils/CommonFixture.hpp"
#include "utils/MultiInstance.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"
#include "utils/TestSandboxId.hpp"
#include "WString.hpp"
#include "launcher/Config.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

typedef appbox::test::CommonFixture E2E_MultiInstance;
using namespace appbox::test;

namespace
{

/** Text the log of a master instance carries once its own program runs. */
constexpr const char* kApplicationStarted = "the initial application of the sandbox was started";

/** Budget of a case which waits for a stage of a run, in milliseconds. */
constexpr int kStageTimeoutMs = 60000;

/** Time the program of the master instance stays alive, in milliseconds. */
constexpr std::uint32_t kShortHoldMs = 1500;

/**
 * @brief Count the runs of a sandbox which started their own program.
 * @param[in] text Log files of the case.
 * @return The number of times a run reported that it started its application.
 */
std::size_t ApplicationStarts(const std::string& text)
{
    std::size_t count = 0;
    std::size_t position = text.find(kApplicationStarted);

    while (position != std::string::npos)
    {
        ++count;
        position = text.find(kApplicationStarted, position + 1);
    }

    return count;
}

} // namespace

/**
 * Condition:
 * 1. A launcher serves a sandbox whose program leaves after a short time, so the
 *    run closes the control pipe of the sandbox while a client arrives.
 * 2. A second launcher of the same archive sends its request in that window.
 *
 * Expected:
 * 1. The request is not lost: the second launcher either hands it over to the
 *    run which still serves the sandbox, or it races for the pipe again and
 *    serves the sandbox itself once the first run left.
 * 2. The client reports neither a timeout nor a refusal, and the program of the
 *    request reports when the request was handed over.
 *
 * The case does not pin which of the two happened, because the window between
 * the last program of a run and the closing of its pipe is what the retry of the
 * shutdown race covers, and the case cannot place its request inside that window
 * on a machine it does not control.
 */
TEST_F(E2E_MultiInstance, ShutdownRace)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());

    const auto master_config = MultiInstanceConfig(cwd, id, "app");

    /*
     * The configuration of the second instance carries the program of the case
     * as well, so the run of that instance starts a program when it becomes the
     * master instance of the sandbox.
     */
    auto secondary_config = MultiInstanceConfig(cwd, id, "request");
    secondary_config.startup.screen = false;
    secondary_config.startups[0].executable = appbox::WideToUTF8(ProbePath(ProbeBitness::Bit64));

    StartupRun  master;
    std::thread master_thread([&]() {
        master =
            ProbeStartupRun(GetCWDString(), master_config, "", false, LogRun{}, false, std::wstring(), kShortHoldMs);
    });

    const auto running = WaitForRunLog(cwd, kApplicationStarted, kStageTimeoutMs);

    StartupRun    request;
    std::uint32_t secondary_exit = 1;
    if (running)
    {
        /* The program of the run leaves after the hold, so the request arrives at its end. */
        std::this_thread::sleep_for(std::chrono::milliseconds(kShortHoldMs - 100));

        secondary_exit =
            ProbeRunSecondary(cwd.wstring(), nlohmann::json(secondary_config), "", "request", 0, LogRun{}, request);
    }

    master_thread.join();

    ASSERT_TRUE(running) << "the master instance never started its application";

    const auto log = ReadRunLog(cwd);

    /* The request was served, either by the first run or by the one which followed it. */
    const bool handed_over = request.reports.size() == 1;
    const bool served_again = ApplicationStarts(log) >= 2;
    EXPECT_TRUE(handed_over || served_again)
        << "the request was neither handed over nor served by a run which started afterwards";

    EXPECT_EQ(secondary_exit, 0u) << "the second instance reported " << secondary_exit;

    /* The run of the first instance is complete, whatever the second one did. */
    EXPECT_EQ(master.exit_code, 0u);
    EXPECT_EQ(master.reports.size(), static_cast<std::size_t>(1)) << master.reports.size();
}
