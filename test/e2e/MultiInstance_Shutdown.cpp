#include "utils/CommonFixture.hpp"
#include "utils/JoinedThread.hpp"
#include "utils/MultiInstance.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"
#include "utils/TestSandboxId.hpp"
#include "WString.hpp"
#include "launcher/Config.hpp"
#include <cstdint>
#include <filesystem>

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
 * 2. The case ends the run of that launcher while a client of the same archive
 *    sends its request, so the request arrives while the run closes the control
 *    pipe of the sandbox.
 *
 * Expected:
 * 1. The request is not lost: the second launcher either hands it over to the
 *    run which still serves the sandbox, or it races for the pipe again and
 *    serves the sandbox itself once the first run left. Neither a timeout nor a
 *    refusal reaches the client, so the client leaves with zero.
 * 2. The run of the first instance is complete.
 *
 * The case does not pin which of the two happened, because the window between
 * the last program of a run and the closing of its pipe is what the retry of the
 * shutdown race covers, and the case cannot place its request inside that window
 * on a machine it does not control. The case ends the program of the run while
 * the request is on its way, so the two overlap without a fixed hold.
 *
 * The code of the second launcher is the whole judgment: a request which is
 * refused leaves with the code of the refusal and one which ran out of its
 * budget leaves with `ERROR_TIMEOUT`, while a request which was served leaves
 * with zero, whether the run which served it was the first one or the client
 * itself.
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
    secondary_config.startups[0].executable =
        appbox::WideToUTF8(ProbePath(ProbeBitness::Bit64)).value_or(std::string());

    StartupRunHandle master(cwd.wstring(), master_config);
    ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
        << "the master instance never started its application";

    const auto& master_reports = master.Collect();
    ASSERT_EQ(master_reports.reports.size(), static_cast<std::size_t>(1)) << master_reports.reports.size();

    /*
     * The request is on its way while the case ends the run: the two overlap, so
     * the request arrives while the sandbox of the run closes instead of after a
     * fixed time.
     */
    StartupRun    request;
    std::uint32_t secondary_exit = 1;
    JoinedThread  requester([&]() {
        secondary_exit =
            ProbeRunSecondary(cwd.wstring(), nlohmann::json(secondary_config), "", "request", 0, LogRun{}, request);
    });

    master.Exit(0);
    const auto master_exit = master.Wait();

    requester.Join();

    /* The request was served by one of the two runs, so the client left with
     * zero instead of the code of a refusal or of a budget which ran out. */
    EXPECT_EQ(secondary_exit, 0u) << "the second instance reported " << secondary_exit;

    /* The run of the first instance is complete, whatever the second one did. */
    EXPECT_EQ(master_exit, 0u);
}

/**
 * Condition:
 * 1. A launcher serves a sandbox and its own program works.
 * 2. The case ends the run of that launcher while a client of the same archive
 *    sends its request, and it repeats that race several times in a row.
 *
 * Expected:
 * 1. No round reports a refusal of the request: a request which is served and a
 *    request which is raced again both leave with zero, so a round which leaves
 *    with another code is a request the sandbox lost.
 * 2. In particular no round reports `ERROR_INVALID_DATA`, which the client
 *    reports for a peer which answered a payload it cannot read. A control pipe
 *    which closes while the request is on its way is a peer which went away and
 *    not a peer which speaks another protocol, so a round which reports that
 *    code means the closure of the sandbox was mistaken for a foreign server.
 * 3. Every run of the first instance is complete.
 *
 * The rounds reuse one sandbox, so a request of a round may be served by the run
 * the previous round left behind: the case pins the code of the client alone,
 * which is what a client of a packaged application reads, and not which of the
 * runs served it.
 */
TEST_F(E2E_MultiInstance, ShutdownRaceReportsNoProtocolMismatch)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());

    /* Number of rounds the case races, which is what a machine of a CI has time for. */
    constexpr int kRounds = 3;

    for (int round = 0; round < kRounds; ++round)
    {
        SCOPED_TRACE(::testing::Message() << "round " << round);

        const auto master_config = MultiInstanceConfig(cwd, id, "app");

        auto secondary_config = MultiInstanceConfig(cwd, id, "request");
        secondary_config.startup.screen = false;
        secondary_config.startups[0].executable =
            appbox::WideToUTF8(ProbePath(ProbeBitness::Bit64)).value_or(std::string());

        StartupRunHandle master(cwd.wstring(), master_config);
        ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
            << "the master instance never started its application";

        StartupRun    request;
        std::uint32_t secondary_exit = 1;
        JoinedThread  requester([&]() {
            secondary_exit =
                ProbeRunSecondary(cwd.wstring(), nlohmann::json(secondary_config), "", "request", 0, LogRun{}, request);
        });

        master.Exit(0);
        const auto master_exit = master.Wait();

        requester.Join();

        /*
         * The client names the reason it left, so a code which is not the one of
         * a served request names the defect of the round.
         */
        EXPECT_EQ(secondary_exit, 0u) << "round " << round << " reported " << secondary_exit;

        /* A control pipe which closed is never a peer which speaks another protocol. */
        EXPECT_NE(secondary_exit, static_cast<std::uint32_t>(ERROR_INVALID_DATA))
            << "round " << round << " reported the closure of the sandbox as a protocol mismatch";

        EXPECT_EQ(master_exit, 0u);
    }
}
