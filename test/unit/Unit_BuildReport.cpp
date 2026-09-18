#include <gtest/gtest.h>
#include "src/core/BuildReport.hpp"
#include <chrono>
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
 * @brief The elapsed time is shown as mm:ss and switches to h:mm:ss only for
 *        runs which last an hour or longer.
 */
TEST(UnitBuildReport, ElapsedTimeUsesMinuteSecondFormat)
{
    using std::chrono::milliseconds;

    EXPECT_EQ(appbox::FormatElapsed(milliseconds(0)), "00:00");
    EXPECT_EQ(appbox::FormatElapsed(milliseconds(12 * 1000)), "00:12");
    EXPECT_EQ(appbox::FormatElapsed(milliseconds(59999)), "00:59");
    EXPECT_EQ(appbox::FormatElapsed(milliseconds(61 * 1000)), "01:01");
    EXPECT_EQ(appbox::FormatElapsed(milliseconds(3600 * 1000)), "1:00:00");
    EXPECT_EQ(appbox::FormatElapsed(milliseconds(3661 * 1000)), "1:01:01");
}

/**
 * @brief A negative duration cannot happen in a run but must not produce a
 *        broken text either.
 */
TEST(UnitBuildReport, ElapsedTimeTreatsNegativeValuesAsZero)
{
    EXPECT_EQ(appbox::FormatElapsed(std::chrono::milliseconds(-5000)), "00:00");
}

/**
 * @brief The preparing stage has no file of its own but still reports the time.
 */
TEST(UnitBuildReport, ProgressMessageNamesThePreparingStage)
{
    const appbox::BuildProgress progress{appbox::BuildStage::Preparing, 0, 340, {}};
    EXPECT_EQ(appbox::BuildProgressMessage(progress, std::chrono::milliseconds(3000)),
              "Preparing the archive... - Elapsed 00:03");
}

/**
 * @brief The packing stage names the file which is being packed on a line of
 *        its own, below the counter line.
 */
TEST(UnitBuildReport, ProgressMessageNamesThePackedFile)
{
    const appbox::BuildProgress progress{appbox::BuildStage::Packing, 12, 340, L"MyApp\\bin\\tool.exe"};
    EXPECT_EQ(appbox::BuildProgressMessage(progress, std::chrono::milliseconds(12000)),
              "Packing files: 12 / 340 - Elapsed 00:12\nMyApp\\bin\\tool.exe");
}

/**
 * @brief The extracting stage is reported with its own verb but the same shape.
 */
TEST(UnitBuildReport, ProgressMessageNamesTheExtractedFile)
{
    const appbox::BuildProgress progress{appbox::BuildStage::Extracting, 3, 340, L"MyApp\\data\\note.txt"};
    EXPECT_EQ(appbox::BuildProgressMessage(progress, std::chrono::milliseconds(60000)),
              "Extracting files: 3 / 340 - Elapsed 01:00\nMyApp\\data\\note.txt");
}

/**
 * @brief Reports without a file or without a known total omit the missing part
 *        instead of showing an empty separator.
 */
TEST(UnitBuildReport, ProgressMessageOmitsMissingParts)
{
    const appbox::BuildProgress without_file{appbox::BuildStage::Packing, 340, 340, {}};
    EXPECT_EQ(appbox::BuildProgressMessage(without_file, std::chrono::milliseconds(1000)),
              "Packing files: 340 / 340 - Elapsed 00:01");

    const appbox::BuildProgress without_total{appbox::BuildStage::Packing, 0, 0, {}};
    EXPECT_EQ(appbox::BuildProgressMessage(without_total, std::chrono::milliseconds(1000)),
              "Packing files... - Elapsed 00:01");
}

/**
 * @brief A single line message is measured by its character count.
 */
TEST(UnitBuildReport, MessageExtentMeasuresSingleLineMessages)
{
    const auto empty = appbox::MeasureMessageExtent("");
    EXPECT_EQ(empty.lines, static_cast<std::size_t>(1));
    EXPECT_EQ(empty.width, static_cast<std::size_t>(0));

    const auto text = appbox::MeasureMessageExtent("Packing files: 12 / 340");
    EXPECT_EQ(text.lines, static_cast<std::size_t>(1));
    EXPECT_EQ(text.width, static_cast<std::size_t>(23));
}

/**
 * @brief The width is the longest line, not the whole message.
 */
TEST(UnitBuildReport, MessageExtentUsesTheLongestLine)
{
    const auto text = appbox::MeasureMessageExtent("short\na much longer line\nmid line");
    EXPECT_EQ(text.lines, static_cast<std::size_t>(3));
    EXPECT_EQ(text.width, static_cast<std::size_t>(18));

    /* A trailing line break starts another line, even when it is empty. */
    const auto trailing = appbox::MeasureMessageExtent("line\n");
    EXPECT_EQ(trailing.lines, static_cast<std::size_t>(2));
    EXPECT_EQ(trailing.width, static_cast<std::size_t>(4));
}

/**
 * @brief Characters of a double byte script count as one character of two
 *        columns, not as the three bytes they need in UTF-8.
 */
TEST(UnitBuildReport, MessageExtentCountsWideCharacters)
{
    /* Two wide characters (six bytes in UTF-8) followed by a plain extension. */
    const std::string wide = "\xE6\x96\x87\xE4\xBB\xB6" ".txt";

    const auto text = appbox::MeasureMessageExtent(wide);
    EXPECT_EQ(text.lines, static_cast<std::size_t>(1));
    EXPECT_EQ(text.width, static_cast<std::size_t>(8));
}

/**
 * @brief Every outcome has its own result text of the build dialog.
 */
TEST(UnitBuildReport, ResultMessageDescribesTheOutcome)
{
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::ArchiveWritten, L"", ""),
              "The archive was written successfully.");
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::ApplicationStarted, L"", ""),
              "The archive was written successfully. The packaged application is starting.");
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::Cancelled, L"", ""),
              "The build run was cancelled; no archive was written.");
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::LaunchFailed, L"", ""),
              "The archive was written, but the packaged loader could not be started.");
}

/**
 * @brief Outcomes which leave an archive behind report where it was written,
 *        with a label which tells what the path is.
 */
TEST(UnitBuildReport, ResultMessageCarriesTheArchivePath)
{
    const std::wstring path = L"C:\\out\\MyApp.zip";

    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::ArchiveWritten, path, ""),
              "The archive was written successfully.\nSaved to: C:\\out\\MyApp.zip");
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::ApplicationStarted, path, ""),
              "The archive was written successfully. The packaged application is starting.\n"
              "Saved to: C:\\out\\MyApp.zip");
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::LaunchFailed, path, ""),
              "The archive was written, but the packaged loader could not be started.\n"
              "Saved to: C:\\out\\MyApp.zip");
}

/**
 * @brief A path is not shown for outcomes which did not write an archive.
 */
TEST(UnitBuildReport, ResultMessageIgnoresThePathOfFailedRuns)
{
    const std::wstring path = L"C:\\out\\MyApp.zip";

    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::Cancelled, path, ""),
              "The build run was cancelled; no archive was written.");
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::Failed, path, "disk is full"),
              "The build run failed: disk is full");
}

/**
 * @brief A failed run reports the reason of the failure.
 */
TEST(UnitBuildReport, ResultMessageCarriesTheFailureReason)
{
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::Failed, L"", "disk is full"),
              "The build run failed: disk is full");

    /* The error of a cancelled run must not leak into a failure message. */
    EXPECT_EQ(appbox::BuildResultMessage(appbox::BuildOutcome::Failed, L"", ""),
              "The build run failed.");
}
