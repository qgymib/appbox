#ifndef APPBOX_TEST_UTILS_KNOWNFOLDER_HPP
#define APPBOX_TEST_UTILS_KNOWNFOLDER_HPP

#include <string>

namespace appbox::test
{

/**
 * @brief Get the path of the known folder
 * @param[in] folder_id Known folder ID
 * @param[in] pure If true, remove the `:` in the return value.
 * @return Path of the known folder
 */
std::wstring GetKnownFolderPath(const std::wstring& folder_id, bool pure);

} // namespace appbox::test

#endif
