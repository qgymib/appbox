#ifndef APPBOX_PACKER_CORE_SNAPSHOT_CAPTURE_HPP
#define APPBOX_PACKER_CORE_SNAPSHOT_CAPTURE_HPP

#include "SnapshotModel.hpp"
#include "SnapshotStore.hpp"
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief One folder of the host the capture walks through.
 *
 * The recorded paths are always relative to the preset directory, so a root
 * which names a folder below the preset directory records the same paths as a
 * root which names the preset directory itself. That is what lets a test point
 * a root at a folder of its own below a known folder of the host and still
 * produce entries which the pack model accepts.
 */
struct CaptureFilesystemRoot
{
    /**
     * @brief Identifier of the preset directory the paths are relative to.
     */
    std::string preset_id;

    /**
     * @brief Host folder to walk through: the preset directory itself or a
     *        folder below it.
     */
    std::wstring scan_path;
};

/**
 * @brief The roots one capture walks through.
 *
 * The structure is passed in instead of being read from the preset table, so a
 * capture can be pointed at a folder of a test as well.
 */
struct CaptureOptions
{
    /**
     * @brief The folders of the filesystem to walk through.
     */
    std::vector<CaptureFilesystemRoot> filesystem_roots;

    /**
     * @brief The names of the registry root keys to walk through.
     */
    std::vector<std::wstring> registry_roots;

    /**
     * @brief Host files the capture must not record.
     *
     * The snapshot file a capture writes grows while the walk runs, so a file
     * which is stored below a preset directory would be reported as an entry
     * the change added and would travel into the archive. The file which is in
     * use is named here and is left out of the state which is read, which is
     * what keeps a snapshot from describing itself.
     */
    std::vector<std::wstring> excluded_paths;
};

/**
 * @brief The part of the machine a capture is reading right now.
 */
enum class CaptureStage
{
    Filesystem, ///< The folders of the preset directories.
    Registry,   ///< The keys of the registry root keys.
    Environment ///< The persisted environment of the machine.
};

/**
 * @brief One progress report of a capture.
 *
 * The total number of entries is not known before the walk is over, so the
 * report carries the number of entries recorded so far and the entry which is
 * being read right now instead of a fraction.
 */
struct CaptureProgress
{
    /**
     * @brief The part of the machine the capture is reading.
     */
    CaptureStage stage = CaptureStage::Filesystem;

    /**
     * @brief Number of entries recorded so far.
     */
    std::size_t done = 0;

    /**
     * @brief Number of folders or keys the capture could not read.
     *
     * A folder or a key which reports an error is skipped instead of failing
     * the capture of a whole machine, so the count tells the user how much of
     * the machine the snapshot is missing.
     */
    std::size_t skipped = 0;

    /**
     * @brief The folder, key or variable which is being read, may be empty.
     */
    std::wstring current;
};

/**
 * @brief Error reported when the progress callback aborted the capture.
 */
inline constexpr const char* kCaptureCancelledError = "cancelled by the user";

/**
 * @brief Progress callback of a capture.
 *
 * The callback is invoked once per folder, key or variable; returning false
 * aborts the capture with kCaptureCancelledError.
 */
using CaptureProgressCallback = std::function<bool(const CaptureProgress&)>;

/**
 * @brief Get the filesystem roots of a capture of the whole machine.
 *
 * One root is produced per preset directory which is resolved on this machine,
 * so the capture reads the folders the filesystem workspace offers.
 *
 * @return The roots, in the order of the preset directories.
 */
std::vector<CaptureFilesystemRoot> DefaultCaptureFilesystemRoots();

/**
 * @brief Get the registry roots of a capture of the whole machine.
 * @return The names of the five root keys of the view.
 */
std::vector<std::wstring> DefaultCaptureRegistryRoots();

/**
 * @brief Read the state of the machine into one phase of a snapshot file.
 *
 * The filesystem is walked folder by folder, the registry key by key and the
 * environment is read from the persisted environment of the machine, which is
 * the pair of the environment keys of `HKEY_LOCAL_MACHINE` and
 * `HKEY_CURRENT_USER` with the references of a value resolved. The persisted
 * environment is used instead of the environment block of the process,
 * because a capture of the state before an installation and a capture of the
 * state after it are usually taken by one run of the packer, whose own
 * environment block never changes in between.
 *
 * Every entry is handed to the writer as soon as it is read, so the state of
 * the machine never travels through the memory of the packer: the memory of a
 * run is the size of one entry and of the page cache of the database.
 *
 * A folder or a key which cannot be read is skipped and counted, so a capture
 * of a whole machine reports the parts it could not read instead of failing:
 * only a root which does not name a preset directory, a folder which is not
 * part of it, or a name which is not a root key of the view is refused.
 *
 * The phase is not marked as a state by this call: the caller commits it with
 * `SnapshotWriter::CommitPhase()` once the walk is over, which is what tells a
 * complete run from one the user cancelled.
 *
 * @param[in] options The roots to walk through.
 * @param[in,out] writer Writer which receives the entries; the phase it writes
 *                was picked by `SnapshotWriter::BeginPhase()`.
 * @param[in] progress Progress callback, may be empty.
 * @param[out] error Error description on failure.
 * @return true on success; the error names kCaptureCancelledError when the
 *         callback aborted the capture.
 */
bool CaptureSnapshot(const CaptureOptions& options, SnapshotWriter& writer, const CaptureProgressCallback& progress,
                     std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_SNAPSHOT_CAPTURE_HPP
