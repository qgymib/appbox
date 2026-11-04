#ifndef APPBOX_CORE_EXTRACTION_CLEANUP_HPP
#define APPBOX_CORE_EXTRACTION_CLEANUP_HPP

#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace appbox
{

/**
 * @brief Bookkeeping of the temporary folders a `Build and Run` run extracts
 *        and has to release once the program started from them is gone.
 *
 * A `Build and Run` run extracts the archive it wrote into a folder below the
 * temporary directory and starts the launcher from there. Windows cannot
 * delete a folder a running program holds open, so the folder belongs to the
 * lifetime of that program: it is released when the program ends, and at once
 * when the program cannot be started at all.
 *
 * The bookkeeping lives below the user interface, because the lifecycle of a
 * temporary folder is a property of the run and not of the window which shows
 * it: the window only reports the process it started and the end of that
 * process. Every entry names a folder the caller created and a process the
 * caller started; the table never looks at a directory of its own, so it
 * cannot reach the `data/cache` folder of a session, which is the one folder
 * of the run that is meant to stay.
 */
class ExtractionCleanup
{
public:
    /**
     * @brief Number of attempts of the removal of a folder.
     *
     * The files of a program which just ended can stay held for a moment by
     * the file system, an on access scanner of an antivirus product or the
     * release of the process itself, so the removal is repeated instead of
     * giving up on the first denial.
     */
    static constexpr int kRemoveAttempts = 10;

    /**
     * @brief Delay between two attempts of the removal, in milliseconds.
     */
    static constexpr int kRemoveDelayMs = 50;

    /**
     * @brief Register a folder which is released when the given process ends.
     *
     * A folder which is registered twice for the same process keeps the entry
     * of the first registration.
     *
     * @param[in] process_id Identifier of the process started from the folder.
     * @param[in] folder Folder to release, an empty path registers nothing.
     */
    void Register(unsigned long process_id, std::filesystem::path folder);

    /**
     * @brief Take the folder registered for a process.
     *
     * The entry is removed from the table, so a caller which takes a folder
     * owns it and the folder is never released twice through the table.
     *
     * @param[in] process_id Identifier of the process.
     * @return The folder of the process, empty when none is registered.
     */
    std::filesystem::path Take(unsigned long process_id);

    /**
     * @brief Take the folders whose process already ended.
     *
     * The call is what a window uses when it goes away while programs started
     * from its folders are still registered: a folder whose program ended is
     * handed back and released, a folder whose program still runs stays in the
     * table, because the running program holds it open.
     *
     * @return The folders of the processes which ended, in registration order.
     */
    std::vector<std::filesystem::path> TakeExited();

    /**
     * @brief Remove a folder with the folders and the files below it.
     *
     * The call is idempotent: a folder which is missing is not an error, and a
     * removal which is denied is repeated a bounded number of times. No
     * exception leaves the call, so it is usable from a destructor.
     *
     * @param[in] folder Folder to remove, an empty path removes nothing.
     */
    static void RemoveFolder(const std::filesystem::path& folder);

    /**
     * @brief Whether a process is still running.
     *
     * @param[in] process_id Identifier of the process, zero names no process.
     * @return true when the process exists and did not end.
     */
    static bool IsProcessAlive(unsigned long process_id);

private:
    /**
     * @brief The folders which wait for the end of their process.
     *
     * The table is guarded by a mutex, because a window reports the end of a
     * process on the thread of the user interface while a pack thread may
     * register a folder of a run which just finished.
     */
    std::mutex mutex_;

    /**
     * @brief The entries of the table, keyed by the process which holds them.
     */
    std::unordered_map<unsigned long, std::filesystem::path> folders_;
};

} // namespace appbox

#endif // APPBOX_CORE_EXTRACTION_CLEANUP_HPP
