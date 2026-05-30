#ifndef APPBOX_FILESYSTEM_DIR_NAME_HPP
#define APPBOX_FILESYSTEM_DIR_NAME_HPP

#include <string>

namespace appbox::filesystem
{

/**
 * @brief Get the directory name of a path.
 * @param[in] path The path to get the directory name from.
 * @return The directory name of the path.
 */
std::wstring DirName(const std::wstring& path);

} // namespace appbox::filesystem

#endif
