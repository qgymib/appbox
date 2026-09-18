#include "BuildReport.hpp"

namespace appbox
{

std::string BuildResultMessage(BuildOutcome outcome, const std::string& error)
{
    switch (outcome)
    {
        case BuildOutcome::ArchiveWritten:
            return "The archive was written successfully.";

        case BuildOutcome::ApplicationStarted:
            return "The archive was written successfully. The packaged application is starting.";

        case BuildOutcome::Cancelled:
            return "The build run was cancelled; no archive was written.";

        case BuildOutcome::Failed:
            return error.empty() ? std::string("The build run failed.")
                                 : "The build run failed: " + error;

        case BuildOutcome::LaunchFailed:
            return "The archive was written, but the packaged loader could not be started.";
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
