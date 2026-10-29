#ifndef APPBOX_COMMON_FILESYSTEM_ISOLATION_POLICY_HPP
#define APPBOX_COMMON_FILESYSTEM_ISOLATION_POLICY_HPP

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
 * | `Merge` | folder | visible | visible | visible |
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
 *
 * `Merge` keeps every layer visible like `WriteCopy`, yet it is the only mode
 * which lets a modification reach the host filesystem. `WritesToHost()` below
 * decides the layer a write or a delete of an entry is applied to; the
 * visibility of the mode is the one of `WriteCopy`, so the resolver masks
 * nothing for it.
 */

/**
 * @brief Whether the isolation mode keeps the host layer out of the view.
 *
 * `WriteCopy` and `Merge` keep the host layer visible, because both are meant
 * to let the sandboxed process see the content of the host filesystem; only
 * `Whiteout` and `Full` of a folder hide it.
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

/**
 * @brief Whether a modification of an entry is applied to the host filesystem.
 *
 * `Merge` is the only mode which lets a modification reach the host
 * filesystem, and it does so by the rule of the mode:
 *
 * - a sandbox layer (the upper layer or a lower layer) holds the entry - the
 *   modification is applied to that layer, which is the upper layer for every
 *   write and delete: the sandbox owns the entry, so the host filesystem keeps
 *   its own content;
 * - no sandbox layer holds the entry - the entry belongs to the host
 *   filesystem, which is modified when it holds the entry and which gains the
 *   entry when no layer holds it at all.
 *
 * The layer which holds the entry therefore decides the layer of the
 * modification, and not the host filesystem: a copy the state of the sandbox
 * carries, which an earlier run of another mode left there, keeps the entry
 * inside the sandbox until the copy is removed. This is the same rule the
 * registry applies with `appbox::registry::ValueWritesToHost()`, and it is what
 * makes the mode a later run observes the mode of the layers of that run: the
 * state of the sandbox never pins the behaviour of a mode the archive or a
 * patch package no longer names.
 *
 * Every other mode redirects every modification into the sandbox, which is
 * what keeps the host filesystem untouched outside `Merge`.
 *
 * @param[in] mode The isolation mode of the closest listed entry.
 * @param[in] sandbox_holds Whether the upper layer or a lower layer holds the
 *                          entry, or hides it with a marker of the view.
 * @return true when the modification has to be applied to the host layer.
 */
inline bool WritesToHost(FilesystemIsolation mode, bool sandbox_holds)
{
    if (mode != FilesystemIsolation::Merge)
    {
        return false;
    }
    return !sandbox_holds;
}

} // namespace filesystem
} // namespace appbox

#endif // APPBOX_COMMON_FILESYSTEM_ISOLATION_POLICY_HPP
