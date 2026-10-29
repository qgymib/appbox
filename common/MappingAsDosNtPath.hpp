#ifndef APPBOX_COMMON_MAPPING_AS_DOS_NT_PATH_HPP
#define APPBOX_COMMON_MAPPING_AS_DOS_NT_PATH_HPP

#include <string>

namespace appbox
{

/**
 * @brief Map a native path of the object manager to a DOS style NT path.
 *
 * A sandboxed process names its objects in the form the object manager reports,
 * while the view of the sandbox and the layers of the archive are keyed by the
 * DOS style path of a drive, so the mapping answers the second form for every
 * name a local file system produces:
 *
 * - `\??\X:\dir\file`, which already is the form of the answer; the letter of
 *   the drive is upper cased.
 * - `\??\Volume{GUID}\dir\file`, the name a volume reports before a drive
 *   letter is assigned to it; the letter the volume is mounted on is looked up.
 * - `\SystemRoot\dir\file`, the name the kernel uses for the directory of
 *   Windows.
 * - `\??\GLOBALROOT\Device\HarddiskVolumeX\dir\file`.
 * - `\Device\HarddiskVolumeX\dir\file`, the name the file system reports for
 *   the object behind a handle.
 *
 * A name of another namespace names no entry of the view and is refused: a UNC
 * path, a network redirector, a named pipe, a mailslot. A path whose drive is
 * not a local file system is refused as well - a network drive, a drive of an
 * unknown type, a volume which is mounted in a directory only - and so is a
 * path which is relative to the current directory of a drive (`\??\X:foo`) or
 * which does not start with a separator.
 *
 * The mapping is shared: the launcher resolves the startup file of a session
 * with it and the sandbox resolves the name of every object a sandboxed process
 * opens, so both sides agree on what a path of the view is.
 *
 * @param[in] path Native path to map.
 * @param[out] mapped DOS style NT path of the entry, written only when the call
 *                   succeeds.
 * @return true when the path names an entry of a local file system.
 */
inline bool MappingAsDosNtPath(const std::wstring& path, std::wstring& mapped);

} // namespace appbox

/*
 * The mapping queries the volumes of the machine, yet the project keeps every
 * implementation in a detail header: the header below is the implementation of
 * the declaration above and is included by this header alone.
 */
#include "details/MappingAsDosNtPathDetail.hpp"

#endif // APPBOX_COMMON_MAPPING_AS_DOS_NT_PATH_HPP
