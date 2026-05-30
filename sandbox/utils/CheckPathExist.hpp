#ifndef APPBOX_SANDBOX_UTILS_CHECK_PATH_EXIST_HPP
#define APPBOX_SANDBOX_UTILS_CHECK_PATH_EXIST_HPP

#include "utils/WinAPI.h"
#include <string>

namespace appbox
{

/**
 * @brief Check if a file exists.
 * @param[in] path NT path to the file.
 * @param[in] Attributes Path attributes.
 * @param[out] info Path information.
 * @return Error code.
 */
NTSTATUS CheckPathExist(const std::wstring& path, ULONG Attributes, PFILE_BASIC_INFORMATION info);

} // namespace appbox

#endif
