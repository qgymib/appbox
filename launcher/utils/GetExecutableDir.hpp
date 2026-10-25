#ifndef APPBOX_UTILS_GET_EXECUTABLE_DIR_HPP
#define APPBOX_UTILS_GET_EXECUTABLE_DIR_HPP

#include <string>

namespace appbox
{

/**
 * @brief Get executable dir.
 * @return Executable dir
 */
std::wstring GetExecutableDir();

/**
 * @brief Get full path of executable.
 * @return Full path of executable
 */
std::wstring GetExecutablePath();

/**
 * @brief Get executable name without extension.
 * @return Executable name.
 */
std::wstring GetExecutableName();

} // namespace appbox

#endif
