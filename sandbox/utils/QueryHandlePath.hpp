#ifndef APPBOX_SANDBOX_UTILS_QUERYHANDLEPATH_HPP
#define APPBOX_SANDBOX_UTILS_QUERYHANDLEPATH_HPP

#include "utils/WinAPI.h"
#include <string>

namespace appbox
{

/**
 * @brief Query the path of a file handle.
 * @param[in] hFile The handle to query.
 * @param[out] path The path of the file handle.
 * @return The status code.
 */
NTSTATUS QueryHandlePath(HANDLE hFile, std::wstring& path);

} // namespace appbox

#endif
