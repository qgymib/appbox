#ifndef APPBOX_LAUNCHER_UTILS_PROCESS_LAUNCHER_HPP
#define APPBOX_LAUNCHER_UTILS_PROCESS_LAUNCHER_HPP

#include "sandbox/utils/WinAPI.h"
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Launch process
 * @param[in] path Executable path
 * @param[in] args Arguments
 * @param[in] hide_console True to start a console program without a console
 *                         window, false to let it show its own.
 * @return 0 if success, otherwise error code.
 */
DWORD ProcessLauncher(const std::wstring& path, const std::vector<std::wstring> args, bool hide_console);

} // namespace appbox

#endif
