#ifndef APPBOX_LAUNCHER_UTILS_MAPBASEFS_HPP
#define APPBOX_LAUNCHER_UTILS_MAPBASEFS_HPP

#include "sandbox/utils/WinAPI.h"
#include "sandbox/Config.hpp"

namespace appbox
{

/**
 * @brief Map the layers of a filesystem domain into the sandbox.
 *
 * Every child directory of the layer root becomes a read-only layer of the
 * view, named after the layer key it maps (`#ProgramFiles#`, a single drive
 * letter, ...). The isolation file of the domain lives in the same folder and
 * is skipped, like the directory names which are reserved for the other
 * isolation domains.
 *
 * @param[in] layer_root Folder which holds one directory per layer key.
 * @param[out] mapped_fs Mapped file systems.
 * @return 0 on success, otherwise error code.
 */
DWORD MapBaseFS(const std::string& layer_root, std::vector<SandboxLowerFS>& mapped_fs);

} // namespace appbox

#endif
