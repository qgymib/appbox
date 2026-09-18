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

/**
 * @brief Get the file name of the executable including its extension.
 *
 * The loader names its configuration file after this value, so the extension
 * is part of the result: an executable `foo.exe` reports `foo.exe`.
 *
 * @return File name of the executable, e.g. `foo.exe`.
 */
std::wstring GetExecutableFileName();

/**
 * @brief Build the default configuration path of one executable.
 *
 * The configuration file lives beside the executable and carries its full
 * file name with a `.json` suffix appended, so `C:\app\foo.exe` resolves to
 * `C:\app\foo.exe.json`. The suffix is appended instead of replacing the
 * extension, so the `.exe` part survives.
 *
 * @param[in] executable_path Path of the executable.
 * @return Path of the default configuration file.
 */
std::wstring DefaultConfigPathForExecutable(const std::wstring& executable_path);

} // namespace appbox

#endif
