#ifndef APPBOX_LOADER_UTILS_KNOWNFOLDER_HPP
#define APPBOX_LOADER_UTILS_KNOWNFOLDER_HPP

#include <string>

namespace appbox
{

/**
 * @brief Search for a known folder by name.
 *
 * The name is a `#Name#` delimited layer key such as `#ProgramFiles#`; the
 * comparison is case sensitive and must match the whole key.
 *
 * @param[in] name Layer key of the folder.
 * @param[out] folder_path Folder path.
 * @return true if the folder is found, false otherwise.
 */
bool SearchFolderID(const std::wstring& name, std::wstring& folder_path);

/**
 * @brief Expand a known folder path.
 *
 * A path which starts with a `#Name#` layer key such as
 * `#ProgramFiles#\MyApp\app.exe` is rewritten to the real path of the folder
 * plus the remainder of the path. Every other path, including a plain
 * absolute path, is returned unchanged. The layer key is matched case
 * insensitively and without a leading separator, so the remainder keeps its
 * own separator and a doubled backslash is never produced.
 *
 * @param[in] path File path.
 * @return Expanded file path.
 */
std::wstring ExpandKnownFolder(const std::wstring& path);

} // namespace appbox

#endif
