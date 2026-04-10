#ifndef APPBOX_LOADER_BUILD_COMMANDLINE_HPP
#define APPBOX_LOADER_BUILD_COMMANDLINE_HPP

#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Build a command line string from an executable path and arguments.
 * @param[in] exe Path to the executable.
 * @param[in] args List of command line arguments.
 * @return Command line string with quotes and escaping as needed.
 */
std::wstring BuildCommandLine(const std::wstring& exe, const std::vector<std::wstring>& args);

} // namespace appbox

#endif // APPBOX_LOADER_BUILD_COMMANDLINE_HPP
