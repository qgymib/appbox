#ifndef APPBOX_LAUNCHER_UTILS_STARTUP_LAYOUT_HPP
#define APPBOX_LAUNCHER_UTILS_STARTUP_LAYOUT_HPP

#include <windows.h>
#include <cstddef>

namespace appbox
{

/**
 * @brief Stage of a run the startup screen reports.
 *
 * The stages are the phases of the launcher itself, in the order they run in:
 * the screen advances its progress bar stage by stage, so a user of a packaged
 * application sees which part of the preparation is going on.
 */
enum class StartupStage
{
    Preparing,          ///< The configuration is resolved and the modules are checked.
    LoadingPatches,     ///< The patch packages are read, verified and extracted.
    MountingSandbox,    ///< The filesystem layers, the hive and the RPC server are set up.
    StartingApplication ///< The processes of the run are created.
};

/**
 * @brief Get the number of stages the screen reports.
 * @return The number of stages.
 */
std::size_t StartupStageCount();

/**
 * @brief Get the overall progress of one stage.
 *
 * Every stage owns a slice of the progress bar and the fraction advances the
 * bar inside that slice, so a run which reports the boundaries of its stages
 * only still shows a bar which grows in steps. The values are clamped, so a
 * caller cannot move the bar outside the slice of its stage.
 *
 * @param[in] stage Stage of the run.
 * @param[in] fraction Progress inside the stage, `0` at its beginning and `1`
 *                     at its end.
 * @return The overall progress in `[0, 1]`.
 */
double StartupStageProgress(StartupStage stage, double fraction);

/**
 * @brief Layout of the startup screen.
 *
 * The rectangles are client coordinates of the window: the name on top, the
 * image below it and the progress bar at the bottom. A session which sets no
 * image leaves `image` and the two image dimensions empty, so the window only
 * carries the name and the bar.
 *
 * The image is scaled down while it does not fit into the room the screen
 * gives it, so a large logo never leaves the screen; a small image keeps its
 * own size and is centered.
 */
struct StartupLayout
{
    int width = 0;  ///< Width of the client area of the window.
    int height = 0; ///< Height of the client area of the window.

    RECT name = {};  ///< Room of the name of the application.
    RECT image = {}; ///< Room of the startup image, empty while none is set.
    RECT bar = {};   ///< Room of the progress bar.

    int bar_height = 0;   ///< Height of the progress bar.
    int image_width = 0;  ///< Width the image is drawn with, zero while none is set.
    int image_height = 0; ///< Height the image is drawn with, zero while none is set.
};

/**
 * @brief Compute the layout of the startup screen.
 *
 * The layout is a pure function of the sizes it is given, so it can be checked
 * without a window: the screen only draws the rectangles it returns.
 *
 * @param[in] image_width Width of the startup image in pixels, zero while the
 *                        session sets no image.
 * @param[in] image_height Height of the startup image in pixels, zero while
 *                         the session sets no image.
 * @param[in] name_height Height the name of the application needs, which the
 *                        caller measures with the font of the screen.
 * @param[in] work_area Work area of the monitor the screen is shown on.
 * @param[in] dpi Resolution of the monitor, in dots per inch.
 * @return The layout of the screen.
 */
StartupLayout ComputeStartupLayout(int image_width, int image_height, int name_height, const RECT& work_area,
                                   unsigned int dpi);

} // namespace appbox

#endif // APPBOX_LAUNCHER_UTILS_STARTUP_LAYOUT_HPP
