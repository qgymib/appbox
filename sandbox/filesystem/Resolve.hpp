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
     * path. Without such an entry the field keeps `WriteCopy`, which is the
     * default of a folder and the behaviour of a sandbox without an isolation
     * file: every layer of the view stays visible.
     */
    FilesystemIsolation isolation = FilesystemIsolation::WriteCopy;

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

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ResolveOption, bStopOnFirstFound, NameAttributes)
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
