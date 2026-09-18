#ifndef APPBOX_PACKER_CORE_BUILD_REPORT_HPP
#define APPBOX_PACKER_CORE_BUILD_REPORT_HPP

#include <chrono>
#include <cstddef>
#include <functional>
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
 * @brief Stage of a build run which reports file level progress.
 */
enum class BuildStage
{
    /**
     * @brief The archive is prepared: the loader payload and its configuration
     *        are added before the first imported file is packed.
     */
    Preparing,

    /**
     * @brief Imported files are added to the archive.
     */
    Packing,

    /**
     * @brief The archive is extracted again, which only the
     *        `Build and Run` command does.
     */
    Extracting,
};

/**
 * @brief One file level progress report of a build run.
 *
 * The report carries the file which is being handled right now, so the build
 * progress dialog can name it instead of only showing a counter. Both the
 * packing and the extracting half of a run report through this structure.
 */
struct BuildProgress
{
    /**
     * @brief Stage the report belongs to.
     */
    BuildStage stage = BuildStage::Preparing;

    /**
     * @brief Number of files handled so far in the current stage.
     */
    std::size_t done = 0;

    /**
     * @brief Total number of files of the current stage, 0 when it is unknown.
     */
    std::size_t total = 0;

    /**
     * @brief Path of the file which is being handled, relative to the import.
     *
     * The path is reported as `<import name>\<path below the import>` so it
     * stays readable for deeply nested host folders. An empty path means that
     * no single file is being handled, e.g. while the archive is prepared.
     */
    std::wstring current;
};

/**
 * @brief Error reported when the progress callback aborted the run.
 */
inline constexpr const char* kBuildCancelledError = "cancelled by the user";

/**
 * @brief Progress callback of a build run.
 *
 * The callback is invoked once per handled file; returning false aborts the
 * run with kBuildCancelledError.
 */
using BuildProgressCallback = std::function<bool(const BuildProgress&)>;

/**
 * @brief Room a message needs inside the build progress dialog.
 *
 * The native dialog keeps the size it was created with and does not grow when
 * a later message is longer, which would push its buttons out of the visible
 * area; the measured room tells the dialog when a message needs more space.
 */
struct MessageExtent
{
    /**
     * @brief Number of lines of the message.
     */
    std::size_t lines = 1;

    /**
     * @brief Display width of the longest line.
     */
    std::size_t width = 0;
};

/**
 * @brief Measure the room a message needs inside the build progress dialog.
 *
 * The width counts characters and not bytes: a character outside of the ASCII
 * range is a single character of a double byte script and occupies two columns
 * of the dialog font.
 *
 * @param[in] text UTF-8 message text.
 * @return Room the message needs.
 */
MessageExtent MeasureMessageExtent(const std::string& text);

/**
 * @brief Format an elapsed duration for the build progress dialog.
 *
 * The result is `mm:ss` and switches to `h:mm:ss` once the run lasts an hour
 * or longer, so the text keeps a fixed width in the common case.
 *
 * @param[in] elapsed Elapsed time; negative values are treated as zero.
 * @return UTF-8 text, e.g. `"00:12"`.
 */
std::string FormatElapsed(std::chrono::milliseconds elapsed);

/**
 * @brief Build the progress text of the build progress dialog.
 *
 * The first line names the stage, the number of handled files and the elapsed
 * time, so a long run does not only show a counter. The file which is being
 * handled follows on a line of its own, which the dialog shows as a detail
 * line in a smaller font. The elapsed time is part of the text because the
 * dialog itself is created without the timing flags of wxWidgets, which would
 * add a collapsible details area the user has to open first.
 *
 * @param[in] progress Progress report of the running stage.
 * @param[in] elapsed Time since the run started.
 * @return UTF-8 message text.
 */
std::string BuildProgressMessage(const BuildProgress& progress, std::chrono::milliseconds elapsed);

/**
 * @brief Build the result text of the build progress dialog.
 *
 * The text is shown in the same dialog which reported the packing progress
 * before, so it replaces the progress message of that dialog. Outcomes which
 * leave an archive behind also report the path of that archive on a line of
 * its own, introduced by a label which tells what the path is, so the user
 * does not have to look it up in the output box of the window.
 *
 * @param[in] outcome Final state of the build run.
 * @param[in] archive_path Path of the written archive, empty when the run did
 *                         not produce one.
 * @param[in] error Error description, only used by BuildOutcome::Failed.
 * @return UTF-8 message text.
 */
std::string BuildResultMessage(BuildOutcome outcome, const std::wstring& archive_path,
                               const std::string& error);

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
