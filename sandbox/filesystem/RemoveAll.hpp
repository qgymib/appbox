#ifndef APPBOX_SANDBOX_FILESYSTEM_REMOVEALL_HPP
#define APPBOX_SANDBOX_FILESYSTEM_REMOVEALL_HPP

#include "utils/WinAPI.h"
#include <string>

namespace appbox::filesystem
{

/**
 * @brief Remove all files and directories in the specified path, including the path itself.
 * @param[in] path NT path to remove. It canbe a file or a (non-empty) directory.
 * @param[in] Attributes Attributes of the path. E.g. OBJ_CASE_INSENSITIVE
 * @return true if successful, false otherwise.
 */
NTSTATUS RemoveAll(const std::wstring& path, ULONG Attributes);

} // namespace appbox::filesystem

#endif
