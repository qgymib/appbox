#include <gtest/gtest.h>
#include "src/core/BuildReport.hpp"
#include <string>

/**
 * @brief Progress reports below the maximum are passed through unchanged.
 */
TEST(UnitBuildReport, ProgressBelowMaximumIsPassedThrough)
{
    EXPECT_EQ(appbox::BuildProgressValue(0, 10), 0);
    EXPECT_EQ(appbox::BuildProgressValue(1, 10), 1);
    EXPECT_EQ(appbox::BuildProgressValue(9, 10), 9);
}

/**
 * @brief The maximum of the dialog is reserved for the finished state, so a
 *        progress report which reaches or exceeds it is clamped below it.
 */
TEST(UnitBuildReport, ProgressKeepsMaximumForTheFinishedState)
{
    EXPECT_EQ(appbox::BuildProgressValue(10, 10), 9);
    EXPECT_EQ(appbox::BuildProgressValue(11, 10), 9);
    EXPECT_EQ(appbox::BuildProgressValue(1000, 10), 9);
    EXPECT_EQ(appbox::BuildProgressValue(1, 1), 0);
}

/**
 * @brief A non positive range cannot be mapped onto a progress value.
 */
TEST(UnitBuildReport, ProgressHandlesNonPositiveRange)
{
    EXPECT_EQ(appbox::BuildProgressValue(0, 0), 0);
    EXPECT_EQ(appbox::BuildProgressValue(5, 0), 0);
    EXPECT_EQ(appbox::BuildProgressValue(5, -3), 0);
}

/**
 * @brief Every outcome has its own result text of the build dialog.
 */
TEST(UnitBuildReport, ResultMessageDescribesTheOutcome)
{
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::ArchiveWritten, ""),
              "The archive was written successfully.");
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::ApplicationStarted, ""),
              "The archive was written successfully. The packaged application is starting.");
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::Cancelled, ""),
              "The build run was cancelled; no archive was written.");
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::LaunchFailed, ""),
              "The archive was written, but the packaged loader could not be started.");
}

/**
 * @brief A failed run reports the reason of the failure.
 */
TEST(UnitBuildReport, ResultMessageCarriesTheFailureReason)
{
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::Failed, "disk is full"),
              "The build run failed: disk is full");

    /* The error of a cancelled run must not leak into a failure message. */
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::Failed, ""),
              "The build run failed.");
}
