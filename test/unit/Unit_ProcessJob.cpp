#include "utils/ProcessJob.hpp" /* Must be first include file */
#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <vector>

/**
 * @brief The exit code of a job which was not started is zero.
 */
TEST(UnitProcessJob, InitialExitCodeIsZero)
{
    appbox::SandboxConfig cfg;
    appbox::ProcessJob    job(L"appbox_unit_missing.exe", {}, cfg);

    EXPECT_EQ(job.GetExitCode(), 0u);
}

/**
 * @brief Waiting without a completion event has to report a timeout instead of
 *        a successful wait.
 */
TEST(UnitProcessJob, WaitWithoutEventReportsTimeout)
{
    appbox::SandboxConfig cfg;
    appbox::ProcessJob    job(L"appbox_unit_missing.exe", {}, cfg);

    EXPECT_EQ(job.Wait(0), ERROR_TIMEOUT);
}

/**
 * @brief Repeated waits keep reporting the timeout and never hang.
 */
TEST(UnitProcessJob, RepeatedWaitReportsTimeout)
{
    appbox::SandboxConfig cfg;
    appbox::ProcessJob    job(L"appbox_unit_missing.exe", {}, cfg);

    for (int i = 0; i < 3; ++i)
    {
        EXPECT_EQ(job.Wait(0), ERROR_TIMEOUT);
    }
}
