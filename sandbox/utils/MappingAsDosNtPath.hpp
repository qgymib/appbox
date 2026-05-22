#ifndef APPBOX_SANDBOX_UTILS_MAPPING_AS_DOS_NT_PATH_HPP
#define APPBOX_SANDBOX_UTILS_MAPPING_AS_DOS_NT_PATH_HPP

#include <string>

namespace appbox
{

/**
 * @brief Map a NT path to a DOS style NT path.
 * @param[in] path NT path
 * @param[out] mapped DOS style NT path
 * @return true if success, false otherwise
 */
bool MappingAsDosNtPath(const std::wstring& path, std::wstring& mapped);

} // namespace appbox

#endif
