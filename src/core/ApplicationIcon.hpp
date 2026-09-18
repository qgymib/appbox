#ifndef APPBOX_PACKER_CORE_APPLICATION_ICON_HPP
#define APPBOX_PACKER_CORE_APPLICATION_ICON_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Append the file icon of an application to a loader payload.
 *
 * The packer stores the loader under the file name of the startup file of the
 * packaged application, so the extracted archive looks like the application
 * itself. This function continues that idea for the icon: the icon group which
 * the shell shows for @p application_path is copied into the payload image, so
 * Explorer shows the icon of the packaged application for the extracted
 * program.
 *
 * The icon group is *added* to the payload, it does not replace anything:
 *
 * - the added group carries a resource name which the resource directory
 *   orders before the named icon groups of the loader (the directory sorts the
 *   named groups alphabetically, and the shell shows the first one for the
 *   file), so the shell prefers the icon of the application;
 * - the loader keeps its own icon resources (`IDI_ICON1` and the wxWidgets
 *   `wxICON_*` groups), so an icon which the loader loads by name is still the
 *   icon of the loader;
 * - the icon images are written below the numeric ids which the payload does
 *   not use yet, so the images of the existing groups stay intact;
 * - the payload is only used when the added group really became the first
 *   group of the file, otherwise the payload is reported as unchanged.
 *
 * A payload which cannot be patched - not a PE image, an application without
 * an icon, a failing resource update - is not an error of the pack run: the
 * function then returns an empty vector and describes the reason in
 * @p warning, and the caller packs the original payload.
 *
 * The function uses the Windows resource APIs only (LoadLibraryExW with
 * LOAD_LIBRARY_AS_DATAFILE, BeginUpdateResourceW, UpdateResourceW and
 * EndUpdateResourceW); because those work on files, the payload is written to
 * a temporary file below the temporary directory, patched there and read back.
 * The temporary file is removed on every path, including the error paths.
 *
 * @param[in] loader_bytes Embedded loader payload.
 * @param[in] loader_size Payload size in bytes.
 * @param[in] application_path Host path of the main program executable.
 * @param[out] warning Reason why the icon was not applied, empty on success.
 * @return The patched PE image, empty when the payload stays unchanged.
 */
std::vector<char> ApplyApplicationIcon(const void* loader_bytes, std::size_t loader_size,
                                       const std::wstring& application_path, std::string& warning);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_APPLICATION_ICON_HPP
