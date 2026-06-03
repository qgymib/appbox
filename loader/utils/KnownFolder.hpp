#ifndef APPBOX_LOADER_UTILS_KNOWNFOLDER_HPP
#define APPBOX_LOADER_UTILS_KNOWNFOLDER_HPP

#include <string>

namespace appbox
{

/**
 * @brief Search for a known folder by name.
 * @param[in] name Folder name.
 * @param[out] folder_path Folder path.
 * @return true if the folder is found, false otherwise.
 */
bool SearchFolderID(const std::wstring& name, std::wstring& folder_path);

/**
 * @brief Expand a known folder path.
 * @param[in] path File path.
 * @return Expanded file path.
 */
std::wstring ExpandKnownFolder(const std::wstring& path);

} // namespace appbox

#endif
