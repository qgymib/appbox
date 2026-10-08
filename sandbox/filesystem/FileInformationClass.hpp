#ifndef APPBOX_SANDBOX_FILESYSTEM_FILE_INFORMATION_CLASS_HPP
#define APPBOX_SANDBOX_FILESYSTEM_FILE_INFORMATION_CLASS_HPP

#include "utils/WinAPI.h" /* Must be first include file */
#include <nlohmann/json.hpp>

namespace appbox::filesystem
{

/**
 * @brief Name of an information class of the file entry points.
 *
 * The two entry points which carry an information class (`NtQueryInformationFile`
 * and `NtSetInformationFile`) report the class in their trace, so the table is
 * shared instead of being spelled out by both hooks.
 *
 * @param[in] FileInformationClass Class of the call.
 * @return The name of the class, or its numeric value when the table does not
 *         know it.
 */
nlohmann::json FileInformationClassName(FILE_INFORMATION_CLASS FileInformationClass);

/**
 * @brief Whether a class of `NtSetInformationFile` carries a path of its own.
 *
 * The rename and the link families carry the name the object is moved to or
 * linked at, so the hook has to resolve that name through the view before the
 * call reaches the file system. Every other class acts on the handle, which
 * already denotes the layer the view selected, so it is forwarded unchanged.
 *
 * @param[in] FileInformationClass Class of the call.
 * @return true for `FileRenameInformation`, `FileRenameInformationEx`,
 *         `FileLinkInformation`, `FileLinkInformationEx` and their
 *         `...BypassAccessCheck` forms, false for every other class.
 */
bool SetInformationCarriesPath(FILE_INFORMATION_CLASS FileInformationClass);

/**
 * @brief Whether a class of `NtQueryInformationFile` reports a name.
 *
 * The classes below carry the path of the object the handle denotes, which the
 * hook answers with the path of the view instead of the path of the layer the
 * handle was opened in. Every other class reports a property of the object and
 * is forwarded unchanged.
 *
 * @param[in] FileInformationClass Class of the call.
 * @return true for `FileNameInformation`, `FileNormalizedNameInformation` and
 *         `FileAllInformation`, false for every other class.
 */
bool QueryInformationCarriesName(FILE_INFORMATION_CLASS FileInformationClass);

} // namespace appbox::filesystem

#endif // APPBOX_SANDBOX_FILESYSTEM_FILE_INFORMATION_CLASS_HPP
