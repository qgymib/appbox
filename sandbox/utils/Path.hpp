#ifndef APPBOX_SANDBOX_UTILS_PATH_HPP
#define APPBOX_SANDBOX_UTILS_PATH_HPP

#include <string>

namespace appbox
{

/**
 * @brief Convert NT path to DOS path
 * @param[in] nt_path NT path
 * @return DOS path
 */
std::wstring NtPathToDos(const std::wstring& nt_path);

/**
 * @brief Convert DOS path to NT path
 * @param[in] dos_path DOS path
 * @return NT path
 */
std::wstring DosPathToNt(const std::wstring& dos_path);

}

#endif //APPBOX_SANDBOX_UTILS_PATH_HPP
