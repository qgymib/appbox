#include "StartupLayout.hpp"

namespace
{

/**
 * @brief The stages of a run, with the slice of the progress bar each owns.
 *
 * The table is the single place which spells the stages: the order of the
 * screen, the number of stages and the progress of every stage are derived
 * from it, so the three cannot drift apart.
 */
struct StartupStageEntry
{
    appbox::StartupStage stage;  ///< The stage.
    double               weight; ///< Slice of the progress bar the stage owns.
};

/** The stages of a run, in the order they run in. */
constexpr StartupStageEntry kStartupStages[] = {
    { appbox::StartupStage::ExtractingPayload,   0.25 },
    { appbox::StartupStage::Preparing,           0.05 },
    { appbox::StartupStage::LoadingPatches,      0.40 },
    { appbox::StartupStage::MountingSandbox,     0.20 },
    { appbox::StartupStage::StartingApplication, 0.10 },
};

/** Number of stages of the table above. */
constexpr std::size_t kStartupStageCount = sizeof(kStartupStages) / sizeof(kStartupStages[0]);

/** Margin around the content of the screen, in pixels at 96 DPI. */
constexpr int kStartupMargin = 24;

/** Gap between two blocks of the screen, in pixels at 96 DPI. */
constexpr int kStartupGap = 18;

/** Height of the progress bar, in pixels at 96 DPI. */
constexpr int kStartupBarHeight = 6;

/** Minimum width of the content of the screen, in pixels at 96 DPI. */
constexpr int kStartupMinContentWidth = 320;

/** Fraction of the work area the image may use, in percent. */
constexpr int kStartupImageMaxPercent = 60;

/**
 * @brief Scale a length which is spelled for 96 DPI.
 * @param[in] value Length in pixels at 96 DPI.
 * @param[in] dpi Resolution of the monitor.
 * @return The length in pixels of that monitor, at least one pixel.
 */
int Scaled(int value, unsigned int dpi)
{
    const auto scaled = value * static_cast<int>(dpi) / 96;
    return scaled > 0 ? scaled : 1;
}

/**
 * @brief Clamp a fraction into the range of a stage.
 * @param[in] fraction Fraction to clamp.
 * @return The fraction in `[0, 1]`.
 */
double ClampFraction(double fraction)
{
    if (fraction < 0.0)
    {
        return 0.0;
    }
    if (fraction > 1.0)
    {
        return 1.0;
    }
    return fraction;
}

} // namespace

namespace appbox
{

std::size_t StartupStageCount()
{
    return kStartupStageCount;
}

double StartupStageProgress(StartupStage stage, double fraction)
{
    const auto clamped = ClampFraction(fraction);

    double progress = 0.0;
    for (const auto& entry : kStartupStages)
    {
        if (entry.stage == stage)
        {
            return progress + entry.weight * clamped;
        }

        progress += entry.weight;
    }

    /* An unknown stage reports the end of the run, so the bar never stalls. */
    return 1.0;
}

StartupLayout ComputeStartupLayout(int image_width, int image_height, int name_height, const RECT& work_area,
                                   unsigned int dpi)
{
    StartupLayout layout;

    const int margin = Scaled(kStartupMargin, dpi);
    const int gap = Scaled(kStartupGap, dpi);
    const int min_content = Scaled(kStartupMinContentWidth, dpi);
    layout.bar_height = Scaled(kStartupBarHeight, dpi);

    const int work_width = work_area.right - work_area.left;
    const int work_height = work_area.bottom - work_area.top;

    /*
     * The image is scaled down while it is wider or taller than the room the
     * screen gives it, which keeps a large logo inside the screen while a
     * small one keeps its own size. Only a size of both dimensions describes
     * an image which can be drawn, so an incomplete one is no image at all.
     */
    int scaled_width = image_width > 0 ? image_width : 0;
    int scaled_height = image_height > 0 ? image_height : 0;
    if (scaled_width == 0 || scaled_height == 0)
    {
        scaled_width = 0;
        scaled_height = 0;
    }

    const int max_width = work_width * kStartupImageMaxPercent / 100;
    const int max_height = work_height * kStartupImageMaxPercent / 100;

    if (scaled_width > 0 && scaled_height > 0 && (scaled_width > max_width || scaled_height > max_height))
    {
        const double width_factor = static_cast<double>(max_width) / scaled_width;
        const double height_factor = static_cast<double>(max_height) / scaled_height;
        const double factor = width_factor < height_factor ? width_factor : height_factor;

        const int fitted_width = static_cast<int>(scaled_width * factor);
        const int fitted_height = static_cast<int>(scaled_height * factor);
        scaled_width = fitted_width > 0 ? fitted_width : 1;
        scaled_height = fitted_height > 0 ? fitted_height : 1;
    }

    layout.image_width = scaled_width;
    layout.image_height = scaled_height;

    const int content = scaled_width > min_content ? scaled_width : min_content;
    const int name_block = name_height > 0 ? name_height : 1;

    layout.width = content + 2 * margin;

    int height = margin + name_block;
    if (scaled_height > 0)
    {
        height += gap + scaled_height;
    }
    height += gap + layout.bar_height + margin;
    layout.height = height;

    layout.name.left = margin;
    layout.name.top = margin;
    layout.name.right = margin + content;
    layout.name.bottom = margin + name_block;

    if (scaled_height > 0)
    {
        const int top = layout.name.bottom + gap;
        const int left = margin + (content - scaled_width) / 2;

        layout.image.left = left;
        layout.image.top = top;
        layout.image.right = left + scaled_width;
        layout.image.bottom = top + scaled_height;
    }

    layout.bar.left = margin;
    layout.bar.right = margin + content;
    layout.bar.bottom = layout.height - margin;
    layout.bar.top = layout.bar.bottom - layout.bar_height;

    return layout;
}

} // namespace appbox
