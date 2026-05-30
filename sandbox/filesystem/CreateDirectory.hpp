#ifndef APPBOX_SANDBOX_FILESYSTEM_CREATE_DIRECTORY_HPP
#define APPBOX_SANDBOX_FILESYSTEM_CREATE_DIRECTORY_HPP

#include <string>

namespace appbox::filesystem
{

/**
 * @brief Create directories recursively.
 * @param[in] path The path to create directories.
 * @param[in] offset The starting position for splitting.
 * @return True if the directories were created successfully, false otherwise.
 */
bool CreateDirectories(const std::wstring& path, size_t offset);

} // namespace appbox::filesystem

#endif
