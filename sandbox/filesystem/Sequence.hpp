#ifndef APPBOX_SANDBOX_FILESYSTEM_SEQUENCE_HPP
#define APPBOX_SANDBOX_FILESYSTEM_SEQUENCE_HPP

#include <string>
#include <vector>

namespace appbox::filesystem
{

/**
 * @brief Split a string path into a sequence of directory paths.
 * @param[in] str The input string to split.
 * @param[in] offset The starting position for splitting.
 * @param[in] delimiter The delimiter string used for splitting.
 * @param[in] bIncludeLast Whether to include the last path in the sequence.
 * @return A vector of directory paths.
 */
std::vector<std::wstring> Sequence(const std::wstring& path, size_t offset, const std::wstring& delimiter,
                                   bool bIncludeLast);

} // namespace appbox::filesystem

#endif
