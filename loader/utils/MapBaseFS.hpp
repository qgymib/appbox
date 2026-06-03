#ifndef APPBOX_LOADER_UTILS_MAPBASEFS_HPP
#define APPBOX_LOADER_UTILS_MAPBASEFS_HPP

#include "sandbox/utils/WinAPI.h"
#include "sandbox/Config.hpp"

namespace appbox
{

/**
 * @brief Map host file system into sandbox.
 * @param[in] fs Host file system.
 * @param[out] mapped_fs Mapped file systems.
 * @return 0 on success, otherwise error code.
 */
DWORD MapBaseFS(const std::string& fs, std::vector<SandboxLowerFS>& mapped_fs);

}

#endif
