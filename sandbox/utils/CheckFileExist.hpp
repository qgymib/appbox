#ifndef APPBOX_SANDBOX_UTILS_CHECK_FILE_EXIST_HPP
#define APPBOX_SANDBOX_UTILS_CHECK_FILE_EXIST_HPP

#include <string>

namespace appbox
{

/**
 * @brief Check if a file exists.
 * @param[in] path NT path to the file.
 * @return true if the file exists, false otherwise.
 */
bool CheckFileExist(const std::wstring& path);

} // namespace appbox

#endif
