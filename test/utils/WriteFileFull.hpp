#ifndef APPBOX_TEST_UTILS_WRITEFILEFULL_HPP
#define APPBOX_TEST_UTILS_WRITEFILEFULL_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief Write data to file.
 * @param[in] path File path.
 * @param[in] data Data to write.
 * @return Error code.
 * @{
 */
DWORD WriteFileFull(const std::wstring& path, const std::vector<uint8_t>& data);
DWORD WriteFileFull(const std::wstring& path, const std::string& data);
/**
 * @}
 */

} // namespace appbox::test

#endif
