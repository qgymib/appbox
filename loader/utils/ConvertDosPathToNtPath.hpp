#ifndef APPBOX_UTILS_CONVERT_DOS_PATH_TO_NT_PATH_HPP
#define APPBOX_UTILS_CONVERT_DOS_PATH_TO_NT_PATH_HPP

#include "sandbox/utils/WinAPI.h"
#include <string>

namespace appbox
{

/**
 * @brief Convert DOS path to NT path.
 * @param[in] dos_path DOS path.
 * @param[out] nt_path NT path.
 * @return 0 on success, otherwise error code.
 * @{
 */
DWORD ConvertDosPathToNtPath(const std::wstring& dos_path, std::wstring& nt_path);
DWORD ConvertDosPathToNtPath(const std::string& dos_path, std::string& nt_path);
/**
 * @}
 */

} // namespace appbox

#endif
