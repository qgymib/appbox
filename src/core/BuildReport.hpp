#ifndef APPBOX_PACKER_CORE_BUILD_REPORT_HPP
#define APPBOX_PACKER_CORE_BUILD_REPORT_HPP

#include <cstddef>
#include <string>

namespace appbox
{

/**
 * @brief Final state of one build run.
 *
 * The state drives the message shown inside the build progress dialog once
 * the background pack thread finished.
 */
enum class BuildOutcome
{
    /**
     * @brief The build finished and the archive was written.
     */
    ArchiveWritten,

    /**
     * @brief The build finished and the packaged application was started.
     */
    ApplicationStarted,

    /**
     * @brief The user aborted the run; no archive was written.
     */
    Cancelled,

    /**
     * @brief Packing or extracting failed, see the error description.
     */
    Failed,

    /**
     * @brief The archive was written but the packaged loader did not start.
     */
    LaunchFailed,
};

/**
 * @brief Build the result text of the build progress dialog.
 *
 * The text is shown in the same dialog which reported the packing progress
 * before, so it replaces the progress message of that dialog.
 *
 * @param[in] outcome Final state of the build run.
 * @param[in] error Error description, only used by BuildOutcome::Failed.
 * @return UTF-8 message text.
 */
std::string BuildResultMessage(BuildOutcome outcome, const std::string& error);

/**
 * @brief Map a packing progress report onto the value of the progress dialog.
 *
 * The maximum value of the dialog is reserved for the finished state: reaching
 * it turns the dialog into the modal completion state, so a progress report
 * must never reach it while the run is still going on.
 *
 * @param[in] done Number of files packed so far.
 * @param[in] range Maximum value of the progress dialog.
 * @return A value in `[0, range - 1]`, or 0 when range is not positive.
 */
int BuildProgressValue(std::size_t done, int range);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_BUILD_REPORT_HPP
