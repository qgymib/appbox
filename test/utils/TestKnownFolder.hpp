#ifndef APPBOX_TEST_UTILS_TESTKNOWNFOLDER_HPP
#define APPBOX_TEST_UTILS_TESTKNOWNFOLDER_HPP

#include <string>

namespace appbox::test
{

/**
 * @brief Get the path of the known folder
 *
 * The helper belongs to the end-to-end cases. It is named `TestKnownFolder`
 * instead of `KnownFolder` because the launcher ships a header of that name with
 * a different API (`appbox::SearchFolderID` and `appbox::ExpandKnownFolder`),
 * and the single test executable compiles both sides.
 *
 * @param[in] folder_id Known folder ID, for example `#USERPROFILE#`.
 * @param[in] pure If true, remove the `:` in the return value.
 * @return Path of the known folder.
 */
std::wstring GetKnownFolderPath(const std::wstring& folder_id, bool pure);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_TESTKNOWNFOLDER_HPP
