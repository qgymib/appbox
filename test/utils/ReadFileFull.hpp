#ifndef APPBOX_TEST_UTILS_READFILEFULL_HPP
#define APPBOX_TEST_UTILS_READFILEFULL_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief Read the full file
 * @param[in] path File path
 * @param[out] data File data
 * @return Error code
 */
DWORD ReadFileFull(const std::wstring& path, std::string& data);

/**
 * @brief Read the full file
 * @param[in] path File path
 * @param[out] data File data
 * @return Error code
 */
DWORD ReadFileFull(const std::wstring& path, std::vector<uint8_t>& bytes);

} // namespace appbox::test

#endif
