#ifndef APPBOX_COMMON_RESOLVE_CORE_HPP
#define APPBOX_COMMON_RESOLVE_CORE_HPP

#include "WinAPI.h" /* Must be first include file */
#include "ResolveTypes.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace appbox::filesystem
{

/**
 * @brief Size of the header of the data of a reparse point.
 *
 * The header carries the tag, the length of the data behind it and a reserved
 * field. The two variants the view understands follow it with the four fields
 * which count the two names of the target, and the symbolic link adds the flags
 * which mark a relative target behind them.
 */
inline constexpr std::size_t kReparseHeaderSize = sizeof(ULONG) + 2 * sizeof(USHORT);

/** Offset of the name buffer of a mount point. */
inline constexpr std::size_t kMountPointPathBufferOffset = kReparseHeaderSize + 4 * sizeof(USHORT);

/** Offset of the name buffer of a symbolic link, which carries its flags first. */
inline constexpr std::size_t kSymbolicLinkPathBufferOffset = kMountPointPathBufferOffset + sizeof(ULONG);

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
 * @brief Access to the file system the shared resolver needs.
 *
 * The rules of the view - the layer which holds an entry, the markers which
 * hide it and the isolation mode which masks a layer - are implemented once and
 * run in two modules: the sandbox resolves the paths of the calls it hooks,
 * while the launcher resolves the image of a startup file before it creates the
 * process, because that image usually exists in the layers of the archive only.
 *
 * The two modules reach the file system in different ways: the sandbox calls
 * the entry points it saved before it attached its hooks, the launcher calls the
 * system directly. The structure hands that access to the shared
 * implementation, so both sides decide a path with the very same rules.
 */
struct ResolveOps
{
    /**
     * @brief Look up the attributes of one path.
     *
     * The path is a path of a layer or of the host, in the form of the file
     * system, so the implementation must not resolve it in the view again.
     *
     * @param[in] path Path to look up.
     * @param[in] attributes Attributes of the lookup, `OBJ_CASE_INSENSITIVE`
     *                       for a path of the view.
     * @param[out] info Attributes of the entry, may be null when the caller
     *                  only asks whether the entry exists.
     * @return `STATUS_SUCCESS` or the failure of the lookup, which the resolver
     *         treats as a missing entry.
     */
    NTSTATUS (*query_attributes)(const std::wstring& path, ULONG attributes, PFILE_BASIC_INFORMATION info);

    /**
     * @brief Read the data of a reparse point.
     *
     * The object is opened without following it, so the file system reports the
     * link itself instead of the object it names.
     *
     * @param[in] layer_path Path of the object inside its layer.
     * @param[out] data Data of the reparse point, in the layout of
     *                  `REPARSE_DATA_BUFFER`.
     * @return `STATUS_SUCCESS`, `STATUS_NOT_A_REPARSE_POINT` when the object
     *         carries no data of a reparse point, or the failure of the file
     *         system.
     */
    NTSTATUS (*read_reparse_data)(const std::wstring& layer_path, std::vector<BYTE>& data);

    /**
     * @brief Report a diagnostic of the resolution, may be null.
     *
     * The call is used for a path which cannot be resolved, for example a
     * reparse point whose target is no path of the view.
     */
    void (*log_warn)(const std::wstring& message);

    /**
     * @brief Report a trace of the resolution, may be null.
     *
     * The call is used for a step which is worth following, for example the
     * expansion of a reparse point.
     */
    void (*log_debug)(const std::wstring& message);
};

/**
 * @brief The target of a reparse point the view resolves.
 */
struct ReparseTarget
{
    /** Name the file system follows. */
    std::wstring substitute;

    /** Name the shell shows, may be empty. */
    std::wstring print;

    /** Whether the name is relative to the directory of the link. */
    bool relative = false;
};

/**
 * @brief Tag of the data of a reparse point.
 * @param[in] data Data of a reparse point.
 * @return The tag, zero when the data is too short to carry one.
 */
ULONG ReparseTagOf(const std::vector<BYTE>& data);

/**
 * @brief Read the target of the data of a reparse point.
 * @param[in] data Data of the reparse point.
 * @param[out] target The target the data carries.
 * @return true when the data carries a target the view understands.
 */
bool ReadReparseTarget(const std::vector<BYTE>& data, ReparseTarget& target);

/**
 * @brief Whether a reparse point redirects the namespace of the view.
 *
 * Only two of the tags the file system knows name an object of the file system
 * instead of the entry which carries them, and only those have to be resolved
 * by the view: a mount point (a junction and a volume which is mounted into a
 * folder) and a symbolic link. Every other tag describes the entry itself - a
 * placeholder of a cloud store, a container image layer, a packaged
 * application - and the object the entry names is the entry the layers hold, so
 * the view forwards it unchanged.
 *
 * @param[in] tag Tag of a reparse point.
 * @return true when the tag names a target the view has to resolve.
 */
bool IsRedirectingReparseTag(ULONG tag);

/**
 * @brief Resolve a path of the view.
 *
 * The path is mapped to the layers of the view by text: the upper layer first,
 * then the lower layers in the order they are mounted and finally the host
 * layer. Every component of every candidate is looked up, which is what applies
 * the whiteout and opaque markers of the overlay; the hits of the layers an
 * isolation mode hides are masked afterwards. A path which carries a reparse
 * point of a redirecting tag is expanded first, so the isolation of the target
 * decides the answer.
 *
 * @param[in] fs The layers of the view.
 * @param[in] view_path Path of the view to resolve.
 * @param[in] option Lookup option.
 * @param[in] isolation Isolation modes of the view, null for a view without
 *                      an isolation.
 * @param[in] ops Access to the file system, see ResolveOps.
 * @return The resolution result.
 */
ResolveResult::Ptr ResolveCore(const ResolveFs& fs, const std::wstring& view_path, const ResolveOption& option,
                               const IsolationTable* isolation, const ResolveOps& ops);

/**
 * @brief Expand the reparse points of a path of the view.
 *
 * The components of the path are walked from the root and every component which
 * is a reparse point of a redirecting tag is replaced with the target it names,
 * which is resolved again in the view. The result is the path of the view a
 * caller reaches, so the isolation of the target decides the layers a call sees
 * and the layer a modification lands in.
 *
 * A component whose data cannot be read, whose target is no path of the view or
 * whose chain is deeper than `kMaxReparseDepth` fails the call: the view must
 * not let the layer follow a link it did not decide about.
 *
 * @param[in] fs The layers of the view.
 * @param[in] view_path Path of the view to expand.
 * @param[in] mode How far the expansion goes, see ReparseFollowMode.
 * @param[in] name_attributes Lookup attributes of the call.
 * @param[in] isolation Isolation modes of the view, null for a view without an
 *                      isolation.
 * @param[in] ops Access to the file system, see ResolveOps.
 * @param[out] effective_view_path Path of the view the caller reaches, only set
 *                                 when the call succeeds.
 * @return `STATUS_SUCCESS` or the failure of the view.
 */
NTSTATUS ExpandReparsePointsCore(const ResolveFs& fs, const std::wstring& view_path, ReparseFollowMode mode,
                                 ULONG name_attributes, const IsolationTable* isolation, const ResolveOps& ops,
                                 std::wstring& effective_view_path);

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
 * @param[in] link_view_path Path of the view of the link itself.
 * @param[in] fs The layers of the view.
 * @param[out] target_view_path Path of the view the link names, only set when
 *                              the call succeeds.
 * @return `STATUS_SUCCESS`, `STATUS_NOT_A_REPARSE_POINT` when the data carries
 *         no reparse point the view understands, or the failure of the view.
 */
NTSTATUS ReparseTargetToViewPathCore(const std::vector<BYTE>& data, const std::wstring& link_view_path,
                                     const ResolveFs& fs, std::wstring& target_view_path);

/**
 * @brief Turn the target of a reparse point into a path of the view.
 *
 * The target is the name the file system follows, which a caller spells as a
 * path of the object namespace. Two shapes are understood: a path of the view
 * itself, which is what the view stores, and a path of a layer, which a caller
 * may hold because the file system reports the path of the layer for a handle.
 * A device path which names a local volume is turned into the drive of the view
 * as well, because a packed application may carry a link which the host wrote.
 *
 * @param[in] target Name the reparse point follows, as the caller or the layer
 *                   spells it.
 * @param[in] fs The layers of the view.
 * @param[out] view_target Path of the view which the target names, only set when
 *                         the call succeeds.
 * @return `STATUS_SUCCESS`, or `STATUS_REPARSE_POINT_NOT_RESOLVED` when the
 *         target is no path of the view.
 */
NTSTATUS ViewNamespaceTargetCore(const std::wstring& target, const ResolveFs& fs, std::wstring& view_target);

/**
 * @brief Translate the path of a layer back into the path of the view.
 *
 * The helper is the counterpart of the mapping the resolver applies to a view
 * path: the upper layer rebases a view path below its own root by encoding the
 * drive of the view path as the first component of the layer path, and a lower
 * layer rebases the view path of its `mapped_nt_path` below its
 * `host_nt_path`. A path which is below one of those roots is answered with the
 * path of the view it belongs to.
 *
 * Both paths are the form the file system reports for the name of a handle,
 * which starts at the root of the volume of the handle, and the comparison is
 * case insensitive and matches whole components. The upper layer is matched
 * first, then the lower layers in the order they are mounted.
 *
 * @param[in] fs The layers of the view.
 * @param[in] layer_path Path of the upper layer, of a lower layer or of the host
 *                       layer, relative to the root of its volume.
 * @param[out] view_path Path of the view the layer path belongs to, in the same
 *                       form.
 * @return true when the path is below a layer of the view.
 */
bool RebaseLayerPathToViewCore(const ResolveFs& fs, const std::wstring& layer_path, std::wstring& view_path);

} // namespace appbox::filesystem

/*
 * The resolver is the implementation of the declarations above and is compiled
 * into every module which resolves a path of the view - the sandbox and the
 * launcher alike - so the implementation is a header of its own which only this
 * header includes.
 */
#include "details/ResolveCoreDetail.hpp"

#endif // APPBOX_COMMON_RESOLVE_CORE_HPP
