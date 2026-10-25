#include <gtest/gtest.h>
#include "utils/StartupLayout.hpp"
#include <cstddef>
#include <vector>

namespace
{

/** Work area of the cases which check the layout of a full screen. */
const RECT kFullScreen = { 0, 0, 1920, 1080 };

/**
 * @brief Get the stages of a run, in the order they run in.
 * @return The stages.
 */
std::vector<appbox::StartupStage> Stages()
{
    return { appbox::StartupStage::ExtractingPayload, appbox::StartupStage::Preparing,
             appbox::StartupStage::LoadingPatches, appbox::StartupStage::MountingSandbox,
             appbox::StartupStage::StartingApplication };
}

} // namespace

TEST(Unit_StartupLayout, EveryStageOwnsASliceOfTheBar)
{
    const auto stages = Stages();
    ASSERT_EQ(appbox::StartupStageCount(), stages.size());

    double previous = 0.0;
    for (std::size_t index = 0; index < stages.size(); ++index)
    {
        const auto stage = stages[index];

        /* The slice starts where the slice before it ended. */
        EXPECT_DOUBLE_EQ(appbox::StartupStageProgress(stage, 0.0), previous);
        EXPECT_GT(appbox::StartupStageProgress(stage, 1.0), previous);

        previous = appbox::StartupStageProgress(stage, 1.0);
    }

    /* The last stage ends at the end of the bar. */
    EXPECT_DOUBLE_EQ(appbox::StartupStageProgress(stages.back(), 1.0), 1.0);

    /* The first stage starts at the beginning of the bar. */
    EXPECT_DOUBLE_EQ(appbox::StartupStageProgress(stages.front(), 0.0), 0.0);
}

TEST(Unit_StartupLayout, TheBarNeverMovesBackwards)
{
    double previous = 0.0;

    for (const auto stage : Stages())
    {
        for (int step = 0; step <= 10; ++step)
        {
            const auto progress = appbox::StartupStageProgress(stage, static_cast<double>(step) / 10.0);

            EXPECT_GE(progress, previous);
            EXPECT_GE(progress, 0.0);
            EXPECT_LE(progress, 1.0);

            previous = progress;
        }
    }

    EXPECT_DOUBLE_EQ(previous, 1.0);
}

TEST(Unit_StartupLayout, AFractionOutsideTheStageIsClamped)
{
    for (const auto stage : Stages())
    {
        EXPECT_DOUBLE_EQ(appbox::StartupStageProgress(stage, -5.0), appbox::StartupStageProgress(stage, 0.0));
        EXPECT_DOUBLE_EQ(appbox::StartupStageProgress(stage, 5.0), appbox::StartupStageProgress(stage, 1.0));
    }
}

TEST(Unit_StartupLayout, AnUnknownStageReportsTheEndOfTheRun)
{
    /* The bar never stalls, whatever a caller reports. */
    EXPECT_DOUBLE_EQ(appbox::StartupStageProgress(static_cast<appbox::StartupStage>(99), 0.0), 1.0);
}

TEST(Unit_StartupLayout, ALayoutWithoutAnImageCarriesTheNameAndTheBar)
{
    const auto layout = appbox::ComputeStartupLayout(0, 0, 20, kFullScreen, 96);

    /* The content is the minimum width, the name and the bar are the only blocks. */
    EXPECT_EQ(layout.width, 320 + 2 * 24);
    EXPECT_EQ(layout.height, 24 + 20 + 18 + 6 + 24);

    EXPECT_EQ(layout.name.left, 24);
    EXPECT_EQ(layout.name.top, 24);
    EXPECT_EQ(layout.name.right, 24 + 320);
    EXPECT_EQ(layout.name.bottom, 24 + 20);

    EXPECT_EQ(layout.image_width, 0);
    EXPECT_EQ(layout.image_height, 0);
    EXPECT_EQ(layout.image.right - layout.image.left, 0);
    EXPECT_EQ(layout.image.bottom - layout.image.top, 0);

    EXPECT_EQ(layout.bar_height, 6);
    EXPECT_EQ(layout.bar.left, 24);
    EXPECT_EQ(layout.bar.right, 24 + 320);
    EXPECT_EQ(layout.bar.bottom, layout.height - 24);
    EXPECT_EQ(layout.bar.top, layout.bar.bottom - 6);
}

TEST(Unit_StartupLayout, ASmallImageKeepsItsSizeAndIsCentered)
{
    const auto layout = appbox::ComputeStartupLayout(200, 100, 20, kFullScreen, 96);

    EXPECT_EQ(layout.image_width, 200);
    EXPECT_EQ(layout.image_height, 100);

    /* The image is narrower than the content, so it is centered inside it. */
    EXPECT_EQ(layout.image.left, 24 + (320 - 200) / 2);
    EXPECT_EQ(layout.image.right, layout.image.left + 200);

    /* The image sits between the name and the bar. */
    EXPECT_EQ(layout.image.top, layout.name.bottom + 18);
    EXPECT_EQ(layout.image.bottom, layout.image.top + 100);
    EXPECT_EQ(layout.bar.top, layout.image.bottom + 18);
}

TEST(Unit_StartupLayout, AWideImageWidensTheContent)
{
    const auto layout = appbox::ComputeStartupLayout(600, 100, 20, kFullScreen, 96);

    /* The content follows the image while it is wider than the minimum. */
    EXPECT_EQ(layout.image_width, 600);
    EXPECT_EQ(layout.width, 600 + 2 * 24);
    EXPECT_EQ(layout.name.left, 24);
    EXPECT_EQ(layout.name.right, 24 + 600);
    EXPECT_EQ(layout.image.left, 24);
    EXPECT_EQ(layout.bar.right, 24 + 600);
}

TEST(Unit_StartupLayout, ALargeImageIsScaledDownAndKeepsItsAspectRatio)
{
    const RECT work_area = { 0, 0, 1000, 800 };

    const auto layout = appbox::ComputeStartupLayout(4000, 2000, 20, work_area, 96);

    /* The image uses at most 60 percent of the work area. */
    EXPECT_EQ(layout.image_width, 600);
    EXPECT_EQ(layout.image_height, 300);

    /* The aspect ratio of the file survives the scaling. */
    EXPECT_EQ(layout.image_width * 2000, layout.image_height * 4000);

    /* The screen stays inside the work area it was laid out for. */
    EXPECT_LE(layout.width, work_area.right - work_area.left);
    EXPECT_LE(layout.height, work_area.bottom - work_area.top);
}

TEST(Unit_StartupLayout, ATallImageIsScaledDownByItsHeight)
{
    const RECT work_area = { 0, 0, 1000, 800 };

    const auto layout = appbox::ComputeStartupLayout(1000, 4000, 20, work_area, 96);

    /* The height is the limit here, so the width follows from the ratio. */
    EXPECT_EQ(layout.image_height, 480);
    EXPECT_EQ(layout.image_width, 120);
}

TEST(Unit_StartupLayout, AnIncompleteImageSizeYieldsNoImageRoom)
{
    /* Only a size of both dimensions describes an image which can be drawn. */
    const auto no_height = appbox::ComputeStartupLayout(200, 0, 20, kFullScreen, 96);
    EXPECT_EQ(no_height.image_height, 0);
    EXPECT_EQ(no_height.image.right - no_height.image.left, 0);

    const auto no_width = appbox::ComputeStartupLayout(0, 200, 20, kFullScreen, 96);
    EXPECT_EQ(no_width.image_width, 0);
    EXPECT_EQ(no_width.image.bottom - no_width.image.top, 0);
}

TEST(Unit_StartupLayout, TheLayoutGrowsWithTheResolutionOfTheMonitor)
{
    const auto at_96 = appbox::ComputeStartupLayout(0, 0, 20, kFullScreen, 96);
    const auto at_192 = appbox::ComputeStartupLayout(0, 0, 40, kFullScreen, 192);

    /* The margins, the gaps, the bar and the minimum width are scaled. */
    EXPECT_EQ(at_192.width, 640 + 2 * 48);
    EXPECT_EQ(at_192.height, 48 + 40 + 36 + 12 + 48);
    EXPECT_EQ(at_192.bar_height, 12);

    EXPECT_EQ(at_96.bar_height, 6);
}

TEST(Unit_StartupLayout, AnEmptyWorkAreaStillYieldsAUsableScreen)
{
    const RECT empty = { 0, 0, 0, 0 };

    const auto layout = appbox::ComputeStartupLayout(400, 200, 20, empty, 96);

    /* The image is scaled down to at least one pixel and nothing is negative. */
    EXPECT_GE(layout.image_width, 1);
    EXPECT_GE(layout.image_height, 1);
    EXPECT_GT(layout.width, 0);
    EXPECT_GT(layout.height, 0);
    EXPECT_GE(layout.bar.top, layout.name.bottom);
}
