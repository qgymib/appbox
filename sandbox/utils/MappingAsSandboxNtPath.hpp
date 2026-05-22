#ifndef APPBOX_SANDBOX_UTILS_MAPPING_AS_SANDBOX_NT_PATH_HPP
#define APPBOX_SANDBOX_UTILS_MAPPING_AS_SANDBOX_NT_PATH_HPP

#include <string>

namespace appbox
{

/**
 * @brief Map a NT path to a sandbox NT path.
 * @param[in] path NT path
 * @param[in] sandbox sandbox path
 * @param[out] mapped sandbox NT path
 * @return true if success, false otherwise
 */
bool MappingPathInSandbox(const std::wstring& path, const std::wstring& sandbox, std::wstring& out);

} // namespace appbox

#endif
