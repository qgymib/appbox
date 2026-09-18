#ifndef APPBOX_PACKER_CORE_ZIP_READER_HPP
#define APPBOX_PACKER_CORE_ZIP_READER_HPP

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
 * @param[in] zip_path Archive to extract.
 * @param[in] dest_dir Destination folder.
 * @return Error description, empty on success.
 */
std::string ExtractArchive(const std::wstring& zip_path, const std::wstring& dest_dir);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_ZIP_READER_HPP
