#ifndef APPBOX_LAUNCHER_UTILS_VERSION_INFO_HPP
#define APPBOX_LAUNCHER_UTILS_VERSION_INFO_HPP

#include <string>

namespace appbox
{

/**
 * @brief Get the name a program carries in its version resource.
 *
 * The name is the `ProductName` of the resource, which is the field the shell
 * shows for the product; a program which does not carry it falls back to its
 * `FileDescription`, and a program without a version resource at all falls
 * back to its own file name without the extension. The startup screen
 * therefore always has a name to show.
 *
 * The packer writes the file properties of the packaged application into the
 * launcher payload (see `src/core/ApplicationMetadata.hpp`), so a run of an
 * extracted archive reads the name of that application here.
 *
 * @param[in] path Path of the program.
 * @return The name, empty when the path carries no file name at all.
 */
std::wstring ReadImageDisplayName(const std::wstring& path);

/**
 * @brief Get the name of the running program.
 * @return The name of the executable which is running, see
 *         ReadImageDisplayName().
 */
std::wstring ReadOwnDisplayName();

} // namespace appbox

#endif // APPBOX_LAUNCHER_UTILS_VERSION_INFO_HPP
