#ifndef APPBOX_SANDBOX_UTILS_CONVERT_TO_FULL_NT_PATH_HPP
#define APPBOX_SANDBOX_UTILS_CONVERT_TO_FULL_NT_PATH_HPP

#include "utils/WinAPI.h"
#include <string>

namespace appbox
{

/**
 * @brief Convert POBJECT_ATTRIBUTES to full NT path.
 * @param[in] ObjectAttributes Object attributes.
 * @param[in] CreateOptions Create options.  Only `FILE_OPEN_BY_FILE_ID` matters.
 * @param[out] path Full NT path.
 * @return Error code.
 */
NTSTATUS ConvertToFullNtPath(const POBJECT_ATTRIBUTES ObjectAttributes, ULONG CreateOptions, std::wstring& path);

} // namespace appbox

#endif
