#ifndef APPBOX_LAUNCHER_UTILS_HOSTPATH_HPP
#define APPBOX_LAUNCHER_UTILS_HOSTPATH_HPP

#include <string>

namespace appbox
{

/**
 * @brief Normalize a host path so two spellings of one directory compare equal.
 *
 * A launcher of a sandbox names the directory which carries its sandbox in the
 * spelling it read from its own command line, so two launchers of one sandbox
 * may spell the same directory differently: a relative path, a path with a
 * trailing separator or a path with another case. The call expands the path to
 * its absolute form, removes the trailing separators and folds the ASCII letters
 * to lower case, which is the comparison a Windows path asks for.
 *
 * The drive root keeps its separator (`c:\`), so a path which names a drive is
 * not turned into the drive relative form `c:`.
 *
 * @param[in] path Path to normalize, in UTF-16.
 * @return The normalized path; empty when \p path is empty.
 */
std::wstring NormalizeHostPath(const std::wstring& path);

/**
 * @brief Compare two host paths of a sandbox.
 *
 * The two paths are normalized first, so the call answers whether both name the
 * same directory and not whether they are spelled the same.
 *
 * @param[in] left Left operand.
 * @param[in] right Right operand.
 * @return true when both paths name the same directory.
 */
bool SameHostPath(const std::wstring& left, const std::wstring& right);

} // namespace appbox

#endif // APPBOX_LAUNCHER_UTILS_HOSTPATH_HPP
