#ifndef APPBOX_SANDBOX_FILESYSTEM_ISOLATIONPOLICY_HPP
#define APPBOX_SANDBOX_FILESYSTEM_ISOLATIONPOLICY_HPP

#include "FilesystemIsolation.hpp"

namespace appbox
{
namespace filesystem
{

/**
 * @brief Which layers of the view stay visible for an isolated entry.
 *
 * The rules below are the whole policy of the filesystem isolation. The
 * resolver looks up the closest entry of the isolation table at or above the
 * path it resolved, which yields a mode and the kind of the entry that carries
 * it (`source_kind`), and the hooks use the same two values to decide what a
 * call reports:
 *
 * | Mode | Source kind | Host layer | Lower layers | Upper layer |
 * | --- | --- | --- | --- | --- |
 * | `Full` | folder | hidden | visible | visible |
 * | `Full` | file | visible | visible | visible |
 * | `WriteCopy` | folder | visible | visible | visible |
 * | `Whiteout` | folder or file | hidden | hidden | visible |
 *
 * The kind of the source entry matters because `WriteCopy` is a mode of a
 * folder only: for a file it is expressed as `Full`, which keeps the host file
 * readable and sends every write into the sandbox. `Full` of a folder hides
 * the host folder together with its whole subtree, while `Full` of a file
 * keeps the host file readable, which is why the two rows differ.
 *
 * `Whiteout` hides the entry in every layer but the upper one: the sandboxed
 * process may create the entry, the create lands in the overlay, and the entry
 * the sandbox holds itself is visible afterwards while the lower and host
 * entries stay hidden.
 */

/**
 * @brief Whether the isolation mode keeps the host layer out of the view.
 *
 * @param[in] mode The isolation mode of the closest listed entry.
 * @param[in] source_kind The kind of the entry which carries the mode.
 * @return true when a hit of the host layer must not be part of the view.
 */
inline bool HidesHost(FilesystemIsolation mode, FilesystemEntryKind source_kind)
{
    if (mode == FilesystemIsolation::Whiteout)
    {
        return true;
    }

    return mode == FilesystemIsolation::Full && source_kind == FilesystemEntryKind::Directory;
}

/**
 * @brief Whether the isolation mode keeps the lower layers out of the view.
 *
 * Only `Whiteout` hides the content the packer imported: the virtual
 * filesystem is the base of every other mode.
 *
 * @param[in] mode The isolation mode of the closest listed entry.
 * @return true when a hit of a lower layer must not be part of the view.
 */
inline bool HidesLower(FilesystemIsolation mode)
{
    return mode == FilesystemIsolation::Whiteout;
}

/**
 * @brief Whether the entry is invisible until the sandbox holds it itself.
 *
 * An entry which the isolation hides does not exist in the view, so opening,
 * reading, writing and deleting it report `File Not Found`. Creating it is
 * allowed: the object lands in the upper layer and is visible from then on,
 * while the hidden layers stay hidden.
 *
 * @param[in] mode The isolation mode of the closest listed entry.
 * @return true when the entry has no visible layer of its own.
 */
inline bool HidesEntry(FilesystemIsolation mode)
{
    return mode == FilesystemIsolation::Whiteout;
}

} // namespace filesystem
} // namespace appbox

#endif // APPBOX_SANDBOX_FILESYSTEM_ISOLATIONPOLICY_HPP
