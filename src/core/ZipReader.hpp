#ifndef APPBOX_PACKER_CORE_ZIP_READER_HPP
#define APPBOX_PACKER_CORE_ZIP_READER_HPP

#include "BuildReport.hpp"
#include <string>

namespace appbox
{

/**
 * @brief Extract a zip archive into a destination folder.
 *
 * The destination folder is created when it does not exist, and the entry
 * names of the archive are sanitized: absolute paths, drive relative paths
 * and parent references are rejected, so an archive can never write outside
 * of the destination folder. Directory entries become folders, which keeps
 * empty folders of the archive intact.
 *
 * The archive is written by ZipWriter of the packer, so the two functions are
 * the write and read half of the same archive layout.
 *
 * Every extracted file is reported through the progress callback. Directory
 * entries only recreate the folder structure and are not reported, so the
 * total matches the number of file entries of the archive. The reported path
 * drops the `filesystem/<layer key>` prefix of the archive layout, which makes
 * it match the paths the packing stage reports.
 *
 * @param[in] zip_path Archive to extract.
 * @param[in] dest_dir Destination folder.
 * @param[in] progress Called once per extracted file; returning false aborts
 *                     the extraction with kBuildCancelledError. May be empty
 *                     to disable progress reporting.
 * @return Error description, empty on success.
 */
std::string ExtractArchive(const std::wstring& zip_path, const std::wstring& dest_dir,
                           const BuildProgressCallback& progress = {});

} // namespace appbox

#endif // APPBOX_PACKER_CORE_ZIP_READER_HPP
