#ifndef APPBOX_SANDBOX_FILESYSTEM_RESOLVE_HPP
#define APPBOX_SANDBOX_FILESYSTEM_RESOLVE_HPP

#include "utils/WinAPI.h"
#include "IsolationTable.hpp"
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

namespace appbox::filesystem
{

/**
 * @brief How the view treats a reparse point of the path it resolves.
 *
 * A reparse point which redirects the namespace - a junction, a symbolic link
 * or a mount point - is resolved by the view itself: the target is read out of
 * the layer which holds the link and turned into a path of the view, which is
 * then resolved again. That is what makes the isolation of the target decide
 * the layers a caller sees, instead of the isolation of the path the caller
 * spelled, and it keeps the view from reaching an object no layer holds.
 *
 * The mode decides how far that resolution goes: a caller which opens an entry
 * follows the link and reaches the object it names, while a caller which acts
 * on the link itself - a delete, a rename target, an attribute query or an
 * open which asks for the reparse point - has to reach the link.
 */
enum class ReparseFollowMode
{
    /**
     * Every component of the path is expanded, including the entry it names.
     */
    All,

    /**
     * Only the components above the entry are expanded, so a caller which acts
     * on the entry itself reaches the reparse point instead of its target.
     */
    Parent,

    /**
     * No component is expanded.
     *
     * The mode is for a caller which inspects the reparse points of the path
     * itself, and it is what keeps the resolution of a reparse point from
     * recursing into itself.
     */
    None,
};

struct ResolveResult
{
    typedef std::shared_ptr<ResolveResult> Ptr;

    enum class Status
    {
        Exists,            /* File exists. */
        NotFound,          /* File not exists. */
        HiddenByWhiteout,  /* File not found because of whiteout. */
        BlockedByOpaque,   /* File not found because of opaque. */
        HiddenByIsolation, /* File not found because the isolation hides it. */
    };

    struct Path
    {
        /**
         * @brief NT path
         */
        std::wstring fPath;

        /**
         * @brief Path information
         */
        FILE_BASIC_INFORMATION fInfo = {};

        /**
         * @brief Index of the layer which holds the path.
         *
         * 0 is the upper layer (the overlay), 1 to N are the lower layers and
         * the last index is the host layer. The isolation masks the hits of
         * the layers a mode hides, which is why the layer of a hit is
         * recorded.
         */
        size_t layer = 0;
    };

    /**
     * @brief Resolve status.
     */
    Status status = Status::NotFound;

    /**
     * @brief Lookup attributes.
     */
    ULONG NameAttributes = OBJ_CASE_INSENSITIVE;

    /**
     * @brief True if parent path exists.
     */
    bool bParentExist = false;

    /**
     * @brief Actual file path in upper filesystem path.
     * @note The file may not exist.
     */
    std::wstring uPath;

    /**
     * @brief The size of base upper filesystem path that cannot be changed.
     * @note Calculated with std::wstring.
     */
    size_t uPathBaseSize = 0;

    /**
     * @brief True if file is exists in upper filesystem.
     */
    bool bInUpper = false;

    /**
     * @brief Actual file path in upper / lower / host filesystem.
     * @note The file always exists if status == Exists.
     */
    std::vector<Path> hPath;

    /**
     * @brief Index of the host layer inside Path::layer.
     *
     * The candidate list holds the upper layer first, then the lower layers
     * and the host layer last, so the index is the one the host layer of the
     * path carries.
     */
    size_t hostLayer = 0;

    /**
     * @brief Actual file path in the host filesystem.
     *
     * The path is the one a modification of the entry has to be applied to
     * when the isolation of the path writes to the host filesystem, see
     * `WritesToHost()`. The entry may not exist, so the field is set for every
     * resolved path.
     */
    std::wstring hostPath;

    /**
     * @brief The size of base host filesystem path that cannot be changed.
     *
     * The base is the root of the drive of the host path with its separator:
     * the drive root always exists, so a caller which creates the folders above
     * an entry of the host filesystem starts below it.
     *
     * @note Calculated with std::wstring.
     */
    size_t hostPathBaseSize = 0;

    /**
     * @brief True if the host layer holds the file.
     */
    bool bHostHolds = false;

    /**
     * @brief True if the upper layer or a lower layer holds the file.
     */
    bool bSandboxHolds = false;

    /**
     * @brief Whiteout file path in host filesystem.
     */
    std::wstring whiteoutPath;

    /**
     * @brief Opaque file path in host filesystem.
     */
    std::wstring opaquePath;

    /**
     * @brief True if whiteout file is exists in upper filesystem.
     */
    bool bWhiteoutInUpper = false;

    /**
     * @brief True if opaque file is exists in upper filesystem.
     */
    bool bOpaqueInUpper = false;

    /**
     * @brief Isolation mode which applies to the path.
     *
     * The mode of the closest entry of the isolation table at or above the
     * path. Without such an entry the field keeps the default of the view
     * (`Merge`, see `kDefaultIsolation`), which is the behaviour of a sandbox
     * without an isolation file: every layer of the view stays visible and a
     * modification is applied to the host filesystem when the host holds the
     * entry or when no layer holds it at all. A file has no default of its
     * own: the mode of a file path is the mode of the closest entry above it,
     * which is the folder that holds the file.
     */
    FilesystemIsolation isolation = filesystem_isolation::kDefaultIsolation;

    /**
     * @brief Kind of the entry which carries the isolation mode.
     *
     * The mode alone does not say which layers stay visible: `Full` of a
     * folder hides the host folder together with its subtree, while `Full` of
     * a file keeps the host file readable.
     */
    FilesystemEntryKind isolationSource = FilesystemEntryKind::Directory;

    /**
     * @brief Whether an entry of the isolation table decided the mode.
     */
    bool bIsolationListed = false;

    /**
     * @brief Whether the isolation hides a layer of the path.
     *
     * Set when the mode of the path hides the host layer, a lower layer or the
     * entry itself. A call which does not create such an entry reports a
     * missing file instead of a missing path, because the parent directory of
     * the entry is hidden as well: the entry simply does not exist in the
     * view.
     */
    bool bIsolationMasked = false;

    /**
     * @brief The path of the view which was resolved.
     *
     * The reparse points of the requested path are expanded by the view, so
     * this is the path the rest of the result describes: every layer path, the
     * path of the host filesystem and the isolation belong to it. A caller
     * which acts on the entry afterwards - a copy up, the record of a handle,
     * the removal of a marker or the creation of the folders above an entry -
     * has to use it instead of the path it spelled.
     *
     * The field equals the requested path when nothing was expanded, and it
     * carries the path the caller spelled when the expansion failed, see
     * `reparseStatus`.
     */
    std::wstring viewPath;

    /**
     * @brief The failure of a reparse point which the view could not resolve.
     *
     * A reparse point which redirects the namespace and which the view cannot
     * turn into a path of the view - unreadable data, a target which is no
     * path of the view or a chain which is deeper than the limit - fails the
     * call: forwarding it to the layer would let the file system of that layer
     * follow the link and reach an object the view never decided about. The
     * caller has to report this status instead of using the result.
     *
     * The field is `STATUS_SUCCESS` for every other path.
     */
    NTSTATUS reparseStatus = STATUS_SUCCESS;
};
void to_json(nlohmann::json& j, const ResolveResult& r);

struct ResolveOption
{
    /**
     * @brief Stop search when first file is found.
     */
    bool bStopOnFirstFound = true;

    /**
     * @brief Lookup attributes.
     */
    ULONG NameAttributes = OBJ_CASE_INSENSITIVE;

    /**
     * @brief How a reparse point of the path is treated.
     *
     * The default follows every component, which is what a caller that opens
     * an entry wants. A caller which acts on the link itself asks for
     * `Parent`, and the resolution of a reparse point asks for `None`.
     */
    ReparseFollowMode reparseFollow = ReparseFollowMode::All;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ResolveOption, bStopOnFirstFound, NameAttributes, reparseFollow)
};

struct ResolveFsMapping
{
    std::wstring mapped_nt_path;
    std::wstring host_nt_path;
};
void to_json(nlohmann::json& j, const ResolveFsMapping& r);
void from_json(const nlohmann::json& j, ResolveFsMapping& r);

struct ResolveFs
{
    std::wstring                  fs_upper;
    std::vector<ResolveFsMapping> fs_lower;
};
void to_json(nlohmann::json& j, const ResolveFs& r);
void from_json(const nlohmann::json& j, ResolveFs& r);

/**
 * @brief Resolve virtual path to host path.
 * @param[in] vPath Virtual path in mapped view.
 * @param[in] option Resolve option.
 * @return Resolve result.
 */
ResolveResult::Ptr Resolve(const std::wstring& vPath, const ResolveOption& option = {});

/**
 * @brief Resolve virtual path to host path.
 *
 * The path is mapped to the layers by text, and the reparse points of the path
 * are resolved by the view itself first: a junction, a symbolic link or a
 * mount point is followed by reading its target out of the layer which holds
 * it, so the isolation of the target decides the answer instead of the
 * isolation of the path the caller spelled. A reparse point which redirects
 * the namespace and which cannot be turned into a path of the view fails the
 * resolution, see `ResolveResult::reparseStatus`.
 *
 * @param[in] fs Resolve file system.
 * @param[in] vPath Virtual path in mapped view.
 * @param[in] option Resolve option.
 * @param[in] isolation Isolation modes of the virtual filesystem, null when
 *                      the view is resolved without an isolation.
 * @return Resolve result.
 */
ResolveResult::Ptr ResolveFull(const ResolveFs& fs, const std::wstring& vPath, const ResolveOption& option,
                               const IsolationTable* isolation = nullptr);

} // namespace appbox::filesystem

#endif
