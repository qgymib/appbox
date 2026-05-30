#ifndef APPBOX_SANDBOX_FILESYSTEM_REBASE_PATH_HPP
#define APPBOX_SANDBOX_FILESYSTEM_REBASE_PATH_HPP

#include "utils/WinAPI.h" /* Must be first include file */
#include <string>

namespace appbox::filesystem
{

/**
 * @brief Rebase a path to a sandbox path.
 * @param[in] path Virtual view path to rebase. E.g. \?\C:\Windows\System32\cmd.exe
 * @param[in] fs Sandbox path.
 * @param[out] out Rebased path.
 * @return NTSTATUS.
 */
NTSTATUS RebasePath(const std::wstring& path, const std::wstring& fs, std::wstring& out);

} // namespace appbox::filesystem

#endif
