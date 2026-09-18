#ifndef APPBOX_PACKER_CORE_PACK_SERVICE_HPP
#define APPBOX_PACKER_CORE_PACK_SERVICE_HPP

#include "PackModel.hpp"
#include <cstddef>
#include <functional>
#include <string>

namespace appbox
{

/**
 * @brief Error returned by Pack() when the progress callback aborted the run.
 */
inline constexpr const char* kPackCancelledError = "cancelled by the user";

/**
 * @brief Count the regular files below a folder.
 *
 * Symlinks and reparse points are not followed; their targets are not
 * counted separately.
 *
 * @param[in] folder Folder to scan.
 * @return The number of regular files in the whole subtree.
 */
std::size_t CountFilesBelow(const std::wstring& folder);

/**
 * @brief Pack the model into a self-contained zip archive.
 *
 * The archive layout matches the loader runtime conventions:
 *
 * ```
 * AppBoxLoader.exe                       loader payload (loader_bytes)
 * AppBoxLoader.json                      base_fs = ["."], overlay_fs = "data",
 *                                        launch.executable = <layer key>\<import>\<exe>
 * filesystem/<layer key>/<import>/...    imported folder content
 * filesystem/<layer key>/<target>/<file> imported file content
 * ```
 *
 * The loader bytes are supplied by the caller so unit tests can inject a
 * fake payload without a real loader binary.
 *
 * The progress total covers the files of the imported folders plus the
 * individually imported files, so the callback receives a stable upper bound
 * for the whole run.
 *
 * @param[in] model The pack model.
 * @param[in] loader_bytes Embedded AppBoxLoader.exe payload.
 * @param[in] loader_size Payload size in bytes.
 * @param[in] zip_path Destination zip path (truncated when it exists).
 * @param[in] progress Called with (done_files, total_files); returning
 *                     false aborts the pack with kPackCancelledError. May be
 *                     empty to disable progress reporting.
 * @return Error description, empty on success.
 */
std::string Pack(const PackModel& model, const void* loader_bytes, std::size_t loader_size,
                 const std::wstring& zip_path,
                 const std::function<bool(std::size_t, std::size_t)>& progress);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_PACK_SERVICE_HPP
