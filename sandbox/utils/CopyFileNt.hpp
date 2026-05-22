#ifndef APPBOX_SANDBOX_UTILS_COPY_FILE_NT_HPP
#define APPBOX_SANDBOX_UTILS_COPY_FILE_NT_HPP

#include <string>

namespace appbox
{

/**
 * @brief Copy file.
 * @param[in] src  Source file path.
 * @param[in] dst  Destination file path.
 * @return True if success, otherwise false.
 */
bool CopyFileNt(const std::wstring& src, const std::wstring& dst);

} // namespace appbox

#endif
