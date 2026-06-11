#ifndef APPBOX_LOADER_FILESYSTEM_REGCOMPILE_HPP
#define APPBOX_LOADER_FILESYSTEM_REGCOMPILE_HPP

#include "sandbox/utils/WinAPI.h"
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Result of registry hive compilation.
 */
struct RegCompileResult
{
    bool  success;     /* Operation succeeded */
    DWORD error_code;  /* Win32 error code if failed */
};

/**
 * @brief Initialize and compile registry.hive from LowerFS registry.reg files.
 *
 * This function handles:
 * - Loading an existing hive or creating a new empty one
 * - Change detection via SHA-256 hashes stored in __sbx__\Meta\LowerFSHashes
 * - Compiling registry.reg content from all LowerFS layers into the hive
 * - Updating __sbx__ metadata (Modified, Deleted, Whiteout, Opaque markers)
 *
 * The function is idempotent and uses a file-level lock for multi-instance
 * safety (registry.hive.lock). It is safe to call before any sandboxed
 * process has been started.
 *
 * @param[in] hive_path       Absolute path to registry.hive file.
 * @param[in] base_fs_dirs    DOS paths to LowerFS base directories (the
 *                            directories that contain subdirectories such as
 *                            AppData, Desktop, etc. Each may contain a
 *                            registry.reg file in its root).
 * @return Compile result.
 */
RegCompileResult InitializeRegistryHive(const std::wstring& hive_path, const std::vector<std::string>& base_fs_dirs);

} // namespace appbox

#endif // APPBOX_LOADER_FILESYSTEM_REGCOMPILE_HPP