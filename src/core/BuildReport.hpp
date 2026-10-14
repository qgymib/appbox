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
 * The state drives the fields the action dialog shows once the background
 * pack thread finished.
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
     * @brief The archive was written but the packaged launcher did not start.
     */
    LaunchFailed,
};

/**
 * @brief Stage of a build run which reports file level progress.
 */
enum class BuildStage
{
    /**
     * @brief The archive is prepared: the launcher payload and its configuration
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
 * @brief Format an elapsed duration for the elapsed row of an action dialog.
 *
 * The result is `mm:ss` and switches to `h:mm:ss` once the run lasts an hour
 * or longer, so the text keeps a fixed width in the common case.
 *
 * @param[in] elapsed Elapsed time; negative values are treated as zero.
 * @return UTF-8 text, e.g. `"00:12"`.
 */
std::string FormatElapsed(std::chrono::milliseconds elapsed);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_BUILD_REPORT_HPP
