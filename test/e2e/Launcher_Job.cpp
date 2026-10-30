#include "probe/Job.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "launcher/Config.hpp"
#include <windows.h>
#include <cstdint>
#include <filesystem>

typedef appbox::test::CommonFixture E2E_Launcher_Job;
using namespace appbox::test;

namespace
{

/**
 * @brief Build the launcher configuration of a case.
 *
 * The configuration carries the layout of a packed archive: the state of the
 * sandbox below `data` and the read-only resources below `app`.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @return The configuration without any startup file.
 */
appbox::LauncherConfig BuildConfig(const std::filesystem::path& root)
{
    /* clang-format off */
    auto tree = FsRoot(root, {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"filesystem\\#USERPROFILE#", {}) })
    });
    /* clang-format on */

    return tree.Build();
}

} // namespace

/**
 * Condition:
 * 1. The configuration holds no startup file, so the launcher starts the probe
 *    of the case as the program of the run.
 * 2. The probe reports the job object it is a member of.
 *
 * Expected:
 * 1. The program of the run is a member of a job object, which the launcher
 *    names while it creates the process.
 * 2. The job kills its processes when its last handle closes, so a sandbox
 *    cannot outlive the launcher which created it.
 * 3. The job allows no process to break away from it, so the run cannot lose a
 *    process it waits for.
 * 4. The job holds the program of the run. A console program which is started
 *    without a window may share its job with the console host of the system,
 *    so the case only pins that the job holds a process at all.
 */
TEST_F(E2E_Launcher_Job, StartupFileRunsInTheJobOfTheRun)
{
    const auto config = BuildConfig(GetCWD());

    Probe      probe(GetCWD(), config);
    const auto rsp = probe.Call<ProtocolJob>(ProtocolJob::Req{});

    const std::uint32_t flags = rsp.limit_flags;

    EXPECT_TRUE(rsp.in_job) << "the startup file is not a member of a job object";
    EXPECT_NE(flags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, 0u)
        << "the job does not kill its processes when its last handle closes: flags " << flags;
    EXPECT_EQ(flags & JOB_OBJECT_LIMIT_BREAKAWAY_OK, 0u) << "the job allows a process to break away: flags " << flags;
    EXPECT_EQ(flags & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK, 0u)
        << "the job allows a process to break away silently: flags " << flags;
    EXPECT_GE(rsp.active_processes, 1u) << "the job of the run holds no process";
}
