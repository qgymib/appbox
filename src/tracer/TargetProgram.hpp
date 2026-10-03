#ifndef APPBOX_TRACER_TARGETPROGRAM_HPP
#define APPBOX_TRACER_TARGETPROGRAM_HPP

#include <filesystem>

namespace appbox::tracer
{

/**
 * @brief Resolve the program which has to be traced.
 *
 * An existing file is returned as an absolute path. A name without a directory
 * (and a relative path which does not exist) is searched the way CreateProcess
 * searches it: the application directory, the current directory, the system
 * directories and the PATH. Resolving the name here gives a clear error before
 * the debugger is started and gives the report an unambiguous program path.
 *
 * @param[in] target Program as given on the command line.
 * @return Absolute path of the program, or an empty path when it was not found.
 */
std::filesystem::path ResolveTargetProgram(const std::filesystem::path& target);

} // namespace appbox::tracer

#endif // APPBOX_TRACER_TARGETPROGRAM_HPP
