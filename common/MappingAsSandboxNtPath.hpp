#ifndef APPBOX_COMMON_MAPPING_AS_SANDBOX_NT_PATH_HPP
#define APPBOX_COMMON_MAPPING_AS_SANDBOX_NT_PATH_HPP

#include "ErrorCode.hpp"
#include <string>
#include <tl/expected.hpp>

namespace appbox
{

/**
 * @brief Map a path of the view into the upper layer of the sandbox.
 *
 * The upper layer rebases a path of the view below its own root by encoding the
 * drive of the view as the first component of the layer path, so the mapping
 * answers the path of the layer which holds the entry the view path names:
 * `\??\C:\Windows\notepad.exe` becomes
 * `<sandbox>\C\Windows\notepad.exe` for the sandbox root `\??\D:\Overlay`.
 *
 * Both paths have to be absolute paths of a drive, spelled in the form of the
 * object namespace (`\??\C:\dir\file` or `\GLOBAL??\C:\dir\file`), which is the
 * form the file system reports for the name of an object. A path of another
 * namespace - a network share, a named pipe, a device - names no entry of the
 * view, so the call refuses it instead of guessing a layer path.
 *
 * The components of the remainder are normalized, so a caller which spells a
 * parent reference reaches the entry the file system would reach: a reference
 * which climbs above the root of the drive names no entry of the view and
 * fails the call.
 *
 * @param[in] path Path of the view to map.
 * @param[in] sandbox Root of the upper layer, in the same form.
 * @return The path inside the upper layer, or the class of the refusal:
 *         - `kPathNotDosDrivePath` when either path is no absolute path of a
 *           drive in the form of the object namespace;
 *         - `kPathEscapesDriveRoot` when the components of the path climb above
 *           the root of their drive.
 */
inline tl::expected<std::wstring, ErrorCode> MappingPathInSandbox(const std::wstring& path,
                                                                  const std::wstring& sandbox) noexcept;

} // namespace appbox

/*
 * The mapping is pure text handling, yet the project keeps every implementation
 * in a detail header: the header below is the implementation of the declaration
 * above and is included by this header alone.
 */
#include "details/MappingAsSandboxNtPathDetail.hpp"

#endif // APPBOX_COMMON_MAPPING_AS_SANDBOX_NT_PATH_HPP
