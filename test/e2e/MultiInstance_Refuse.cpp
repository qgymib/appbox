#include "utils/CommonFixture.hpp"
#include "utils/MultiInstance.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"
#include "utils/RunState.hpp"
#include "utils/TestSandboxId.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
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
 * 2. A second launcher carries the identifier of that sandbox but lives in
 *    another root: it is a copy of the archive beside the running one.
 *
 * Expected:
 * 1. The request is refused with `sandbox_mismatch`, so the second launcher
 *    leaves with `ERROR_ACCESS_DENIED` (5).
 * 2. The master instance keeps serving: its program still answers the case
 *    after the refused request, the state of the run reports no handled request
 *    and no program of the request, and the run exits with zero.
 *
 * The case holds the run of the master instance, so the program of the run
 * cannot leave on its own and the answer of the case after the request proves
 * that the refused request changed nothing about the run. The client of the
 * case runs the image of the launcher as the user of the sandbox at its
 * integrity level and the server of the pipe is that very launcher, so the code
 * of a refused access can only be the refusal of the root of the request.
 */
TEST_F(E2E_MultiInstance, RefusesDifferentRoot)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto master_config = MultiInstanceConfig(cwd, id, "app");

    StartupRunHandle master(cwd.wstring(), master_config);
    ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
        << "the master instance never started its application";

    const auto& master_reports = master.Collect();
    ASSERT_EQ(master_reports.reports.size(), static_cast<std::size_t>(1)) << master_reports.reports.size();

    /* The second copy of the archive, with the identifier of the running one. */
    const auto      other = cwd / L"other";
    std::error_code ec;
    std::filesystem::create_directories(other, ec);

    const auto secondary_exit = ProbeRunRawConfig(other.wstring(), MultiInstanceRequestDocument(id), LogRun{});

    EXPECT_EQ(master.Ping(), static_cast<std::size_t>(1)) << "the refused request ended the program of the run";

    /*
     * The master instance handled no request and runs its own program alone, so
     * the refused request started nothing.
     */
    appbox::MsgRunState::Rsp state;
    ASSERT_TRUE(QueryRunState(id, state)) << "the state of the run cannot be read";
    EXPECT_EQ(state.handled_requests, static_cast<std::uint64_t>(0));
    ASSERT_EQ(state.programs.size(), static_cast<std::size_t>(1)) << state.programs.size();
    EXPECT_TRUE(state.programs[0].initial) << "the master instance started a program of the refused request";

    master.Exit(0);
    const auto master_exit = master.Wait();

    EXPECT_EQ(secondary_exit, static_cast<std::uint32_t>(ERROR_ACCESS_DENIED));
    EXPECT_EQ(master_exit, 0u);
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
 *    program, holds no state of the second one and its program keeps working.
 */
TEST_F(E2E_MultiInstance, RefusesDifferentSandboxId)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto master_config = MultiInstanceConfig(cwd, id, "app");

    StartupRunHandle master(cwd.wstring(), master_config);
    ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
        << "the master instance never started its application";

    const auto& master_reports = master.Collect();
    ASSERT_EQ(master_reports.reports.size(), static_cast<std::size_t>(1)) << master_reports.reports.size();

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

    const auto exit_code = ProbeRunRawConfig(other.wstring(), nlohmann::json(other_config), LogRun{});

    EXPECT_EQ(master.Ping(), static_cast<std::size_t>(1)) << "the other sandbox ended the program of the run";

    /*
     * The first sandbox ran its own program alone: the state of its run reports
     * no handled request and no program beside the one of the run itself.
     */
    appbox::MsgRunState::Rsp state;
    ASSERT_TRUE(QueryRunState(id, state)) << "the state of the run cannot be read";
    EXPECT_EQ(state.handled_requests, static_cast<std::uint64_t>(0));
    ASSERT_EQ(state.programs.size(), static_cast<std::size_t>(1)) << state.programs.size();
    EXPECT_TRUE(state.programs[0].initial) << "the first sandbox started a program of the other one";

    master.Exit(0);
    const auto master_exit = master.Wait();

    EXPECT_NE(id, other_id);

    /* The second launcher prepared the sandbox of its own directory. */
    EXPECT_EQ(exit_code, 0u);
    EXPECT_TRUE(std::filesystem::exists(other / appbox::layout::kStateDirNameW))
        << "the second launcher did not prepare a sandbox of its own";

    /* The first sandbox ran its own program alone. */
    EXPECT_EQ(master_exit, 0u);
}
