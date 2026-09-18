#include "BuildReport.hpp"
#include "WString.hpp"
#include <cstdio>

namespace
{

/**
 * @brief Label of the line which names the written archive.
 */
constexpr const char* kSavedToLabel = "Saved to: ";

/**
 * @brief Append the archive path to a result text.
 *
 * Only outcomes which leave an archive behind report a path, so an empty path
 * keeps the plain text. The path gets a label of its own because a bare path
 * below the result does not tell what it is.
 *
 * @param[in] text Result text without the path.
 * @param[in] path Path of the written archive, may be empty.
 * @return The result text, with the labelled path on a line of its own when
 *         there is one.
 */
std::string AppendArchivePath(const std::string& text, const std::string& path)
{
    return path.empty() ? text : text + "\n" + kSavedToLabel + path;
}

} // namespace

namespace appbox
{

std::string FormatElapsed(std::chrono::milliseconds elapsed)
{
    if (elapsed.count() < 0)
    {
        elapsed = std::chrono::milliseconds::zero();
    }

    const auto seconds = static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    const auto rest = seconds % 60;

    char buffer[32] = {};
    if (hours > 0)
    {
        std::snprintf(buffer, sizeof(buffer), "%llu:%02llu:%02llu", hours, minutes, rest);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%02llu:%02llu", minutes, rest);
    }

    return std::string(buffer);
}

MessageExtent MeasureMessageExtent(const std::string& text)
{
    MessageExtent extent;

    /*
     * The comparisons are spelled out because <windows.h>, which the wide
     * string helpers pull in, defines max() as a macro.
     */
    std::size_t current = 0;
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const auto byte = static_cast<unsigned char>(text[index]);
        if (byte == '\n')
        {
            if (current > extent.width)
            {
                extent.width = current;
            }
            current = 0;
            extent.lines++;
            continue;
        }

        if (byte < 0x80)
        {
            current++;
            continue;
        }

        /*
         * A multi byte sequence is one character of a double byte script: the
         * continuation bytes belong to it and it occupies two columns.
         */
        current += 2;
        while (index + 1 < text.size()
               && (static_cast<unsigned char>(text[index + 1]) & 0xC0) == 0x80)
        {
            index++;
        }
    }

    if (current > extent.width)
    {
        extent.width = current;
    }

    return extent;
}

std::string BuildProgressMessage(const BuildProgress& progress, std::chrono::milliseconds elapsed)
{
    const auto time = "Elapsed " + FormatElapsed(elapsed);

    if (progress.stage == BuildStage::Preparing)
    {
        /* No single file is handled while the loader payload is prepared. */
        return "Preparing the archive... - " + time;
    }

    const char* const action = progress.stage == BuildStage::Packing ? "Packing files" : "Extracting files";
    std::string text(action);

    if (progress.total > 0)
    {
        text += ": " + std::to_string(progress.done) + " / " + std::to_string(progress.total);
    }
    else
    {
        text += "...";
    }

    text += " - " + time;

    /*
     * The current file gets a line of its own: the native dialog shows the
     * first line as its main instruction and every further line as content, so
     * the file keeps the smaller font of a detail line and the counter line
     * above it keeps its width while the file name changes.
     */
    if (!progress.current.empty())
    {
        text += "\n" + WideToUTF8(progress.current);
    }

    return text;
}

std::string BuildResultMessage(BuildOutcome outcome, const std::wstring& archive_path,
                               const std::string& error)
{
    const auto path = archive_path.empty() ? std::string() : WideToUTF8(archive_path);

    switch (outcome)
    {
        case BuildOutcome::ArchiveWritten:
            return AppendArchivePath("The archive was written successfully.", path);

        case BuildOutcome::ApplicationStarted:
            return AppendArchivePath(
                "The archive was written successfully. The packaged application is starting.", path);

        case BuildOutcome::Cancelled:
            return "The build run was cancelled; no archive was written.";

        case BuildOutcome::Failed:
            return error.empty() ? std::string("The build run failed.")
                                 : "The build run failed: " + error;

        case BuildOutcome::LaunchFailed:
            return AppendArchivePath(
                "The archive was written, but the packaged loader could not be started.", path);
    }

    return "The build run finished.";
}

int BuildProgressValue(std::size_t done, int range)
{
    /* The maximum itself is reserved for the finished state of the dialog. */
    if (range <= 1)
    {
        return 0;
    }

    const auto limit = static_cast<std::size_t>(range - 1);
    return static_cast<int>(done < limit ? done : limit);
}

} // namespace appbox
