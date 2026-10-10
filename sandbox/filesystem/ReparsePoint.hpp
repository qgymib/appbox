#ifndef APPBOX_SANDBOX_FILESYSTEM_REPARSEPOINT_HPP
#define APPBOX_SANDBOX_FILESYSTEM_REPARSEPOINT_HPP

#include "utils/WinAPI.h" /* Must be first include file */
#include "filesystem/Resolve.hpp"
#include <string>
#include <vector>

namespace appbox::filesystem
{

/**
 * @brief Depth limit of a chain of reparse points.
 *
 * A link may name another link, which names a third one: the view follows the
 * chain while it expands the path, and a chain which is longer than the limit
 * is reported as a failure instead of being followed forever. The file system
 * uses the same kind of limit for the same reason.
 */
inline constexpr std::size_t kMaxReparseDepth = 32;

/**
 * @brief Whether a reparse point redirects the namespace of the view.
 *
 * Only two of the tags the file system knows name an object of the file system
 * instead of the entry which carries them, and only those have to be resolved
 * by the view: a mount point (a junction and a volume which is mounted into a
 * folder) and a symbolic link. Every other tag describes the entry itself -
 * a placeholder of a cloud store, a container image layer, a packaged
 * application - and the object the entry names is the entry the layers hold,
 * so the view forwards it unchanged.
 *
 * @param[in] tag Tag of a reparse point.
 * @return true when the tag names a target the view has to resolve.
 */
bool IsRedirectingReparseTag(ULONG tag);

/**
 * @brief Read the data of a reparse point out of a layer.
 *
 * The object is opened with `FILE_OPEN_REPARSE_POINT`, so the file system
 * reports the link itself instead of following it, and its data is read with
 * `FSCTL_GET_REPARSE_POINT`. The caller receives the bytes the file system
 * stores, which is the layout of `REPARSE_DATA_BUFFER`.
 *
 * @param[in] layerPath Path of the object inside its layer.
 * @param[out] data Data of the reparse point.
 * @return `STATUS_SUCCESS`, `STATUS_NOT_A_REPARSE_POINT` when the object
 *         carries no data of a reparse point, or the failure of the file
 *         system.
 */
NTSTATUS ReadReparseData(const std::wstring& layerPath, std::vector<BYTE>& data);

/**
 * @brief Turn a target of a reparse point into a path of the view.
 *
 * The target is the name the file system follows, which a caller spells as a
 * path of the object namespace. Two shapes are understood: a path of the view
 * itself, which is what the view stores, and a path of a layer, which a caller
 * may hold because the file system reports the path of the layer for a handle.
 * A device path which names a local volume is turned into the drive of the
 * view as well, because a packed application may carry a link which the host
 * wrote.
 *
 * @param[in] target Name the reparse point follows, as the caller or the layer
 *                   spells it.
 * @param[out] viewTarget Path of the view which the target names, only set when
 *                        the call succeeds.
 * @return `STATUS_SUCCESS`, or `STATUS_REPARSE_POINT_NOT_RESOLVED` when the
 *         target is no path of the view.
 */
NTSTATUS ViewNamespaceTarget(const std::wstring& target, std::wstring& viewTarget);

/**
 * @brief Turn the target of a reparse point of a link into a path of the view.
 *
 * A symbolic link may name its target relative to the directory of the link
 * (`SYMLINK_FLAG_RELATIVE`), which is the shape a caller creates when it links
 * an entry to a sibling. The relative target is resolved against the directory
 * of the link, because the view maps a layer by replacing the prefix of the
 * view: the components below the link keep their shape, so a relative target
 * names the same entry in the view as it does in the layer.
 *
 * @param[in] data Data of the reparse point.
 * @param[in] linkViewPath Path of the view of the link itself.
 * @param[out] targetViewPath Path of the view the link names, only set when the
 *                            call succeeds.
 * @return `STATUS_SUCCESS`, `STATUS_NOT_A_REPARSE_POINT` when the data carries
 *         no reparse point the view understands, or the failure of the view.
 */
NTSTATUS ReparseTargetToViewPath(const std::vector<BYTE>& data, const std::wstring& linkViewPath,
                                 std::wstring& targetViewPath);

/**
 * @brief Rewrite the data of a reparse point into the namespace of the view.
 *
 * A caller which creates a reparse point spells its target in the namespace it
 * knows, which may be the namespace of a layer: the path the file system
 * reports for a handle is the path of the layer, so an application which links
 * a handle it holds would store the layout of the sandbox. The target is
 * therefore turned into the path of the view before the data is written, which
 * is also the shape the view reads back when it follows the link.
 *
 * A target which cannot be expressed as a path of the view fails the call for
 * a tag which redirects the namespace, because the view could not resolve the
 * link afterwards. The data of every other tag is returned unchanged.
 *
 * @param[in] data Data of the reparse point as the caller wrote it.
 * @param[out] translated Data of the view, only set when the call succeeds.
 * @return `STATUS_SUCCESS`, or the failure of the view.
 */
NTSTATUS TranslateReparseDataToView(const std::vector<BYTE>& data, std::vector<BYTE>& translated);

/**
 * @brief Copy the entry of a layer which carries a reparse point into the
 *        overlay.
 *
 * A link is not copied by the content it reaches: the object of the overlay
 * has to carry the same reparse point, or the view would report a regular file
 * where the layer holds a link, and a later call which acts on the link - the
 * removal of its data, a query of its target - would answer about a different
 * object. The object is created first, a folder for a mount point and a file
 * for a symbolic link, and the data of the source is written into it.
 *
 * @param[in] sourceLayerPath Path of the source inside its layer.
 * @param[in] destinationLayerPath Path the object takes in the overlay.
 * @param[in] sourceAttributes Attributes of the source, which decide the kind
 *                             of the object to create.
 * @return `STATUS_SUCCESS`, or the failure of the file system.
 */
NTSTATUS CopyReparsePointEntry(const std::wstring& sourceLayerPath, const std::wstring& destinationLayerPath,
                               ULONG sourceAttributes);

/**
 * @brief Expand the reparse points of a path of the view.
 *
 * The components of the path are walked from the root and every component
 * which is a reparse point of a redirecting tag is replaced with the target it
 * names, which is resolved again in the view. The result is the path of the
 * view a caller reaches, so the isolation of the target decides the layers a
 * call sees and the layer a modification lands in.
 *
 * A component whose data cannot be read, whose target is no path of the view
 * or whose chain is deeper than `kMaxReparseDepth` fails the call: the view
 * must not let the layer follow a link it did not decide about.
 *
 * @param[in] fs Resolve file system.
 * @param[in] viewPath Path of the view to expand.
 * @param[in] mode How far the expansion goes, see `ReparseFollowMode`.
 * @param[in] nameAttributes Lookup attributes of the call.
 * @param[in] isolation Isolation modes of the virtual filesystem, may be null.
 * @param[out] effectiveViewPath Path of the view which the caller reaches, only
 *                               set when the call succeeds.
 * @return `STATUS_SUCCESS` or the failure of the view.
 */
NTSTATUS ExpandReparsePoints(const ResolveFs& fs, const std::wstring& viewPath, ReparseFollowMode mode,
                             ULONG nameAttributes, const IsolationTable* isolation, std::wstring& effectiveViewPath);

} // namespace appbox::filesystem

#endif // APPBOX_SANDBOX_FILESYSTEM_REPARSEPOINT_HPP
