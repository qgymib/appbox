#include <vector>
#include "filesystem/IsolationPolicy.hpp"
#include "filesystem/MarkerName.hpp"
#include "filesystem/ReparsePoint.hpp"
#include "filesystem/Sequence.hpp"
#include "filesystem/StreamName.hpp"
#include "utils/CheckPathExist.hpp"
#include "utils/Log.hpp"
#include "utils/MappingAsSandboxNtPath.hpp"
#include "Sandbox.hpp"
#include "WString.hpp"
#include "__init__.hpp"
#include "Resolve.hpp"

struct SearchResult
{
    bool whiteout_found = false;
    bool opaque_found = false;

    /**
     * @brief Whether a component of the path carries the attribute of a
     *        reparse point.
     *
     * The flag is a hint: the search already asks every component whether it
     * exists, so the attributes of the parents are read as well and a path
     * which carries no reparse point costs nothing. The expansion of a
     * reparse point runs only when the hint is set, and the hint is
     * deliberately conservative: a component which the isolation hides does
     * not set it, and a path whose parents no layer holds sets it never.
     */
    bool reparse_point_found = false;
};

struct FileLayer
{
    std::wstring base_fs; /* Basis filesystem path. */
    std::wstring file_fs; /* File path based on basis filesystem. */
};
typedef std::vector<FileLayer> FileLayerVec;

/**
 * @brief Whether a view path addresses the root of a drive.
 *
 * The path can carry a "\??\" prefix, only the last two characters matter.
 *
 * @param[in] path View path without trailing separators.
 * @return true when the path is the root of a drive, for example "C:".
 */
static bool IsDriveRoot(const std::wstring& path)
{
    if (path.size() < 2 || path[path.size() - 1] != L':')
    {
        return false;
    }

    const wchar_t letter = path[path.size() - 2];
    return (letter >= L'A' && letter <= L'Z') || (letter >= L'a' && letter <= L'z');
}

/**
 * @brief Mapping file path to upper / lower / host filesystem.
 * @param[in] path Virtual file path. Must has not trailing slash.
 * @return Host file path in upper and lower filesystem.
 */
static FileLayerVec MapViewPathToHost(const appbox::filesystem::ResolveFs& fs, const std::wstring& path)
{
    FileLayerVec ret;

    /* upper fs */
    {
        FileLayer layer;
        layer.base_fs = fs.fs_upper;
        appbox::MappingPathInSandbox(path, layer.base_fs, layer.file_fs);
        ret.push_back(layer);
    }

    /* lower fs */
    for (const auto& mapping : fs.fs_lower)
    {
        FileLayer layer;
        layer.base_fs = mapping.host_nt_path;
        if (appbox::PrefixCompareExchange(path, mapping.mapped_nt_path, layer.base_fs, true, layer.file_fs))
        {
            ret.push_back(layer);
        }
    }

    /* host_fs */
    {
        FileLayer layer;
        layer.base_fs = path.substr(0, path.find(L':'));
        layer.file_fs = path;
        ret.push_back(layer);
    }

    return ret;
}

static void SearchInSingleLayer(const std::vector<std::wstring>& path_seq, size_t layer, bool has_trailing_slash,
                                SearchResult& search_result, appbox::filesystem::ResolveResult& resolve_result)
{
    NTSTATUS   st;
    const auto path_seq_sz = path_seq.size();
    for (size_t j = 0; j < path_seq_sz; j++)
    {
        auto comp_path = path_seq[j];

        /* Check if whiteout file exists. */
        const auto whiteout_path = appbox::filesystem::WhiteoutPathOf(comp_path);
        st = appbox::CheckPathExist(whiteout_path, resolve_result.NameAttributes, nullptr);
        if (NT_SUCCESS(st))
        {
            search_result.whiteout_found = true;
            resolve_result.whiteoutPath = whiteout_path;
            return;
        }

        /*
         * A stream belongs to the file which carries it, so a marker which
         * hides the file hides every stream of it as well. The stream is the
         * last component of its own path, which is why the marker of the file
         * is checked here and not by the loop above: the loop only ever sees
         * the components the caller spelled, and the file is not one of them
         * while the path addresses a stream.
         */
        const auto entry_path = appbox::filesystem::EntryPathOfStream(comp_path);
        if (entry_path != comp_path)
        {
            const auto entry_whiteout_path = appbox::filesystem::WhiteoutPathOf(entry_path);
            st = appbox::CheckPathExist(entry_whiteout_path, resolve_result.NameAttributes, nullptr);
            if (NT_SUCCESS(st))
            {
                search_result.whiteout_found = true;
                resolve_result.whiteoutPath = entry_whiteout_path;
                return;
            }
        }

        /* Check parent path. */
        if (j != path_seq_sz - 1)
        {
            /* If path is drive letter, add backslash. */
            if (j == 0 && comp_path.back() == L':')
            {
                comp_path += L"\\";
            }

            FILE_BASIC_INFORMATION parent_info = {};
            st = appbox::CheckPathExist(comp_path, resolve_result.NameAttributes, &parent_info);
            if (NT_SUCCESS(st))
            { /* All parents must exists. */
                if ((parent_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                { /* The view has to resolve the link itself. */
                    search_result.reparse_point_found = true;
                }

                if (path_seq_sz >= 2 && j == path_seq_sz - 2)
                { /* Mark if direct parent exists. */
                    resolve_result.bParentExist = true;
                }
            }
            else
            {
                break;
            }

            /* For parent path, check if opaque file exists. */
            const auto opaque_path = appbox::filesystem::OpaquePathOf(comp_path);
            st = appbox::CheckPathExist(opaque_path, resolve_result.NameAttributes, nullptr);
            if (NT_SUCCESS(st))
            {
                search_result.opaque_found = true;
                if (resolve_result.opaquePath.empty())
                {
                    resolve_result.opaquePath = opaque_path;
                }
            }
        }
        else
        {
            appbox::filesystem::ResolveResult::Path path;
            path.layer = layer;
            st = appbox::CheckPathExist(comp_path, resolve_result.NameAttributes, &path.fInfo);
            /* For file self, check if exists. */
            if (NT_SUCCESS(st))
            {
                if ((path.fInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                { /* The entry itself is a link the view has to resolve. */
                    search_result.reparse_point_found = true;
                }

                path.fPath = comp_path + (has_trailing_slash ? L"\\" : L"");
                resolve_result.hPath.emplace_back(path);
            }
        }
    }
}

static void SearchInMultipleLayer(const FileLayerVec& path_vec, const appbox::filesystem::ResolveOption& option,
                                  bool has_trailing_slash, SearchResult& search_result,
                                  appbox::filesystem::ResolveResult& resolve_result)
{
    const auto path_vec_sz = path_vec.size();
    for (size_t i = 0; !search_result.whiteout_found && !search_result.opaque_found && i < path_vec_sz; ++i)
    {
        /* Split path sequence into parent directory and self. */
        const auto path_seq =
            appbox::filesystem::Sequence(path_vec[i].file_fs, path_vec[i].base_fs.size(), L"\\", true);

        /* Check each level of path. */
        SearchInSingleLayer(path_seq, i, has_trailing_slash, search_result, resolve_result);

        /* Check if file exists in upper layer. */
        if (i == 0 && !resolve_result.hPath.empty())
        {
            resolve_result.bInUpper = true;
        }

        if (i == 0 && !resolve_result.whiteoutPath.empty())
        {
            resolve_result.bWhiteoutInUpper = true;
            return;
        }
        if (i == 0 && !resolve_result.opaquePath.empty())
        {
            resolve_result.bOpaqueInUpper = true;
        }

        /* Stop on first file. */
        if (option.bStopOnFirstFound && !resolve_result.hPath.empty())
        {
            return;
        }
    }
}

/**
 * @brief Mask the hits of the layers the isolation of a path hides.
 *
 * The search itself stays untouched: every layer is looked up as before, so a
 * parent directory which only the host holds still decides whether the path
 * can be created. The isolation only removes the hits of the layers a mode
 * hides from the result, which is what makes an isolated folder invisible in
 * the host without breaking the creation of its entries.
 *
 * @param[in] isolation Isolation modes of the virtual filesystem, may be null.
 * @param[in] view_path Path which was resolved.
 * @param[in] host_layer Index of the host layer of the candidate list.
 * @param[in,out] resolve_result The result which is masked.
 */
static void ApplyIsolation(const appbox::filesystem::IsolationTable* isolation, const std::wstring& view_path,
                           size_t host_layer, appbox::filesystem::ResolveResult& resolve_result)
{
    if (isolation == nullptr || isolation->Empty())
    {
        return;
    }

    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::WriteCopy;
    appbox::FilesystemEntryKind source_kind = appbox::FilesystemEntryKind::Directory;
    if (!isolation->Lookup(view_path, mode, source_kind))
    {
        /*
         * Without a listed entry the default of the view applies, which the
         * result already carries: it keeps every layer visible and lets a
         * modification reach the host filesystem when the host holds the entry
         * or when no layer holds it, so there is nothing to mask here.
         */
        return;
    }

    resolve_result.isolation = mode;
    resolve_result.isolationSource = source_kind;
    resolve_result.bIsolationListed = true;

    const bool hides_host = appbox::filesystem::HidesHost(mode, source_kind);
    const bool hides_lower = appbox::filesystem::HidesLower(mode);
    resolve_result.bIsolationMasked = hides_host || hides_lower || appbox::filesystem::HidesEntry(mode);
    if (!hides_host && !hides_lower)
    {
        return;
    }

    /*
     * The upper layer is never masked: it holds the objects the sandboxed
     * process created itself, which is what keeps a `Whiteout` entry visible
     * after its creation.
     */
    std::vector<appbox::filesystem::ResolveResult::Path> visible;
    visible.reserve(resolve_result.hPath.size());
    for (const auto& path : resolve_result.hPath)
    {
        if (path.layer != 0 && (hides_lower || (hides_host && path.layer == host_layer)))
        {
            continue;
        }
        visible.push_back(path);
    }
    resolve_result.hPath.swap(visible);
}

namespace appbox::filesystem
{
namespace
{

/**
 * @brief Result of the text based resolution of a path of the view.
 *
 * The resolution maps a path to the layers by text and asks each layer whether
 * the mapped path exists. It does not resolve a reparse point, which is what
 * the caller decides afterwards: the flag says whether the path carries one at
 * all, so the expansion runs only for a path which needs it.
 */
struct ResolvePlainResult
{
    appbox::filesystem::ResolveResult::Ptr result;

    /** Path of the view which was resolved, without a trailing separator. */
    std::wstring viewPath;

    /** Whether a component of the path carries the attribute of a reparse point. */
    bool hasReparsePoint = false;
};

static ResolvePlainResult ResolvePlain(const ResolveFs& fs, const std::wstring& vPath,
                                       const appbox::filesystem::ResolveOption& option, const IsolationTable* isolation)
{
    auto resolve_result = std::make_shared<appbox::filesystem::ResolveResult>();
    resolve_result->status = appbox::filesystem::ResolveResult::Status::Exists;
    resolve_result->NameAttributes = option.NameAttributes;

    /* Remove trailing slash */
    auto copy_v_path = vPath;
    bool has_trailing_slash = false;
    while (!copy_v_path.empty() && copy_v_path.back() == L'\\')
    {
        has_trailing_slash = true;
        copy_v_path.pop_back();
    }

    /*
     * A drive root has to keep its separator. "C:" alone addresses the current
     * directory of the drive, which the layer mapping rejects, so the root
     * would look like a missing file and every caller of Resolve() would report
     * a path which the operating system resolves successfully.
     */
    if (IsDriveRoot(copy_v_path))
    {
        copy_v_path += L'\\';
        has_trailing_slash = false;
    }

    /* Generate host path sequence for upper / lower / host filesystem. */
    const auto path_vec = MapViewPathToHost(fs, copy_v_path);
    resolve_result->uPath = path_vec[0].file_fs + (has_trailing_slash ? L"\\" : L"");
    resolve_result->uPathBaseSize = path_vec[0].base_fs.size();

    /*
     * The last candidate is the host layer: its path is where a modification
     * lands when the isolation of the path writes to the host filesystem,
     * whether the entry exists there or not.
     */
    resolve_result->hostLayer = path_vec.size() - 1;
    resolve_result->hostPath = path_vec[resolve_result->hostLayer].file_fs + (has_trailing_slash ? L"\\" : L"");

    /*
     * The base of the host layer is the root of its drive with the separator,
     * because the drive root always exists: a caller which creates the folders
     * above an entry of the host filesystem therefore starts below it. The
     * candidate list stops at the drive letter, and the drive letter of a
     * process addresses the current directory of that drive, which a caller
     * can neither create nor rely on.
     */
    const auto host_colon = resolve_result->hostPath.find(L':');
    resolve_result->hostPathBaseSize = host_colon == std::wstring::npos ? 0 : host_colon + 2;

    /*
     * A `Merge` path is modified in the host filesystem when the host layer
     * holds the entry or when no layer holds it at all, so the resolver has to
     * know every layer which holds the path and not only the first one. The
     * mode is looked up before the search, because the search itself stops at
     * the first hit while the caller asked for it. A path no entry covers
     * follows the default of the view, which is `Merge` as well, so a caller
     * which may modify such a path asks for every layer through
     * `ResolveOption::bStopOnFirstFound`.
     */
    ResolveOption search_option = option;
    if (search_option.bStopOnFirstFound && isolation != nullptr && !isolation->Empty())
    {
        FilesystemIsolation mode = FilesystemIsolation::WriteCopy;
        FilesystemEntryKind source_kind = FilesystemEntryKind::Directory;
        if (isolation->Lookup(copy_v_path, mode, source_kind) && mode == FilesystemIsolation::Merge)
        {
            search_option.bStopOnFirstFound = false;
        }
    }

    /* For each layer, check if the file exists. */
    SearchResult search_result;
    SearchInMultipleLayer(path_vec, search_option, has_trailing_slash, search_result, *resolve_result);

    /*
     * The isolation of the path decides which of the hits stay visible: the
     * host layer of a `Full` folder and every layer but the upper one of a
     * `Whiteout` entry are masked. The search itself ran over every layer, so
     * a parent directory which only the host holds still decided whether the
     * path can be created.
     */
    ApplyIsolation(isolation, copy_v_path, path_vec.size() - 1, *resolve_result);

    /*
     * The hooks decide the layer a modification is applied to from the two
     * flags: `WritesToHost()` keeps the host filesystem in place when the host
     * layer holds the entry or when no layer holds it at all, and redirects
     * the modification into the upper layer otherwise.
     */
    for (const auto& path : resolve_result->hPath)
    {
        if (path.layer == resolve_result->hostLayer)
        {
            resolve_result->bHostHolds = true;
        }
        else
        {
            resolve_result->bSandboxHolds = true;
        }
    }

    /*
     * The path the result describes is the one which was mapped, so a caller
     * which acts on the entry afterwards does not have to repeat the
     * normalization the resolver applies to the path it was given.
     */
    resolve_result->viewPath = copy_v_path;

    const ResolvePlainResult plain = { resolve_result, copy_v_path, search_result.reparse_point_found };

    /* Fix status. */
    if (resolve_result->hPath.empty())
    {
        if (!resolve_result->whiteoutPath.empty())
        {
            resolve_result->status = appbox::filesystem::ResolveResult::Status::HiddenByWhiteout;
            return plain;
        }

        if (search_result.opaque_found)
        {
            resolve_result->status = appbox::filesystem::ResolveResult::Status::BlockedByOpaque;
            return plain;
        }

        if (resolve_result->bIsolationListed && HidesEntry(resolve_result->isolation))
        {
            /* `Whiteout`: the entry is visible in no layer, so it does not
             * exist in the view until the sandboxed process creates it. */
            resolve_result->status = appbox::filesystem::ResolveResult::Status::HiddenByIsolation;
            return plain;
        }

        resolve_result->status = appbox::filesystem::ResolveResult::Status::NotFound;
    }

    return plain;
}

} // namespace
} // namespace appbox::filesystem

appbox::filesystem::ResolveResult::Ptr appbox::filesystem::ResolveFull(const ResolveFs& fs, const std::wstring& vPath,
                                                                       const ResolveOption&  option,
                                                                       const IsolationTable* isolation)
{
    ResolvePlainResult plain = ResolvePlain(fs, vPath, option, isolation);

    if (option.reparseFollow == ReparseFollowMode::None || !plain.hasReparsePoint)
    {
        return plain.result;
    }

    /*
     * The path carries a reparse point, so the view resolves it itself: the
     * target is read out of the layer which holds the link and turned into a
     * path of the view, which is resolved again. That is what makes the
     * isolation of the target decide the answer instead of the isolation of
     * the path the caller spelled, and it keeps the file system of a layer
     * from following a link the view never decided about.
     */
    std::wstring   expanded;
    const NTSTATUS status =
        ExpandReparsePoints(fs, plain.viewPath, option.reparseFollow, option.NameAttributes, isolation, expanded);
    if (!NT_SUCCESS(status))
    {
        LOG_W(L"failed to resolve the reparse point of {}: {}", plain.viewPath, status);
        plain.result->reparseStatus = status;
        return plain.result;
    }

    if (expanded == plain.viewPath)
    {
        /* The path carries no link which redirects the namespace. */
        return plain.result;
    }

    ResolvePlainResult resolved = ResolvePlain(fs, expanded, option, isolation);
    return resolved.result;
}

appbox::filesystem::ResolveResult::Ptr appbox::filesystem::Resolve(const std::wstring&  vPath,
                                                                   const ResolveOption& option)
{
    return ResolveFull(appbox::sandbox->fs, vPath, option, &appbox::sandbox->fs_isolation);
}

void appbox::filesystem::to_json(nlohmann::json& j, const ResolveFsMapping& r)
{
    j["mapped_nt_path"] = appbox::WideToUTF8(r.mapped_nt_path);
    j["host_nt_path"] = appbox::WideToUTF8(r.host_nt_path);
}

void appbox::filesystem::from_json(const nlohmann::json& j, ResolveFsMapping& r)
{
    r.mapped_nt_path = appbox::UTF8ToWide(j.value("mapped_nt_path", ""));
    r.host_nt_path = appbox::UTF8ToWide(j.value("host_nt_path", ""));
}

void appbox::filesystem::to_json(nlohmann::json& j, const ResolveFs& r)
{
    j["fs_upper"] = appbox::WideToUTF8(r.fs_upper);

    for (auto& ele : r.fs_lower)
    {
        j["fs_lower"].push_back(ele);
    }
}

void appbox::filesystem::from_json(const nlohmann::json& j, ResolveFs& r)
{
    r.fs_upper = appbox::UTF8ToWide(j.value("fs_upper", ""));
    j.at("fs_lower").get_to(r.fs_lower);
}

void appbox::filesystem::to_json(nlohmann::json& j, const ResolveResult& r)
{
    j["status"] = r.status;
    j["bParentExist"] = r.bParentExist;
    j["uPath"] = appbox::WideToUTF8(r.uPath);
    j["uPathBaseSize"] = r.uPathBaseSize;

    j["bInUpper"] = r.bInUpper;
    for (const auto& p : r.hPath)
    {
        j["hPath"].push_back(appbox::WideToUTF8(p.fPath));
    }

    j["whiteoutPath"] = appbox::WideToUTF8(r.whiteoutPath);
    j["opaquePath"] = appbox::WideToUTF8(r.opaquePath);
    j["bWhiteoutInUpper"] = r.bWhiteoutInUpper;
    j["bOpaqueInUpper"] = r.bOpaqueInUpper;

    j["hostLayer"] = r.hostLayer;
    j["hostPath"] = appbox::WideToUTF8(r.hostPath);
    j["bHostHolds"] = r.bHostHolds;
    j["bSandboxHolds"] = r.bSandboxHolds;

    j["bIsolationListed"] = r.bIsolationListed;
    j["bIsolationMasked"] = r.bIsolationMasked;
    j["isolation"] = appbox::filesystem_isolation::IsolationToken(r.isolation);
    j["isolationSource"] = appbox::filesystem_isolation::EntryKindToken(r.isolationSource);

    j["viewPath"] = appbox::WideToUTF8(r.viewPath);
    j["reparseStatus"] = r.reparseStatus;
}
