#include "utils/WinAPI.h" /* Must be first include file */
#include "filesystem/DirName.hpp"
#include "filesystem/IsolationPolicy.hpp"
#include "filesystem/MarkerName.hpp"
#include "filesystem/Sequence.hpp"
#include "filesystem/StreamName.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "utils/MappingAsSandboxNtPath.hpp"
#include "WString.hpp"
#include "ResolveCore.hpp"
#include <cstddef>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace
{

/**
 * @brief What the search of one path found in the layers of the view.
 */
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

/**
 * @brief One candidate of the resolution: the layer root and the path in it.
 */
struct FileLayer
{
    std::wstring base_fs; /* Basis filesystem path. */
    std::wstring file_fs; /* File path based on basis filesystem. */
};
typedef std::vector<FileLayer> FileLayerVec;

/**
 * @brief Result of the text based resolution of a path of the view.
 */
struct ResolvePlainResult
{
    appbox::filesystem::ResolveResult::Ptr result;

    /** Path of the view which was resolved, without a trailing separator. */
    std::wstring viewPath;

    /** Whether a component of the path carries the attribute of a reparse point. */
    bool hasReparsePoint = false;
};

/**
 * @brief Report a diagnostic of the resolution at the warning level.
 * @param[in] ops Operations of the resolution.
 * @param[in] message Message to report.
 */
void Warn(const appbox::filesystem::ResolveOps& ops, const std::wstring& message)
{
    if (ops.log_warn != nullptr)
    {
        ops.log_warn(message);
    }
}

/**
 * @brief Report a trace of the resolution.
 * @param[in] ops Operations of the resolution.
 * @param[in] message Message to report.
 */
void Debug(const appbox::filesystem::ResolveOps& ops, const std::wstring& message)
{
    if (ops.log_debug != nullptr)
    {
        ops.log_debug(message);
    }
}

/**
 * @brief Whether a path starts with a prefix, ignoring the case.
 * @param[in] path Path to inspect.
 * @param[in] prefix Prefix to compare against.
 * @return true when the path starts with the prefix.
 */
bool StartsWithI(const std::wstring& path, const std::wstring& prefix)
{
    if (path.size() < prefix.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < prefix.size(); ++index)
    {
        if (std::towlower(path[index]) != std::towlower(prefix[index]))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Whether a path is below a prefix, on a component boundary.
 *
 * A layer root which is only a prefix of the path without being a whole
 * component must not match: the root `\AppBox\data` does not hold the entry
 * `\AppBox\database\file.txt`.
 *
 * @param[in] path Path to test.
 * @param[in] prefix Prefix to test against.
 * @return true when \p path is \p prefix or a path below it.
 */
bool IsBelowI(const std::wstring& path, const std::wstring& prefix)
{
    if (!StartsWithI(path, prefix))
    {
        return false;
    }

    if (path.size() == prefix.size())
    {
        return true;
    }

    if (!prefix.empty() && prefix.back() == L'\\')
    {
        /* The prefix already ends on a component boundary. */
        return true;
    }

    return path[prefix.size()] == L'\\';
}

/**
 * @brief Namespace which opens a drive in a DOS style NT path.
 * @param[in] dosNtPath Path of the form `\??\C:\dir\file`.
 * @return `\??\`, `\GLOBAL??\` or an empty string when the path has no such
 *         namespace.
 */
std::wstring DosNtNamespace(const std::wstring& dosNtPath)
{
    if (StartsWithI(dosNtPath, L"\\GLOBAL??\\"))
    {
        return L"\\GLOBAL??\\";
    }
    if (StartsWithI(dosNtPath, L"\\??\\"))
    {
        return L"\\??\\";
    }
    return L"";
}

/**
 * @brief Reduce a DOS style NT path to the form the file system reports.
 *
 * The name of a file object starts at the root of its volume, while the paths
 * of the configuration carry the namespace of the object manager and the drive
 * of the view. The helper removes both, so the two forms can be compared.
 *
 * @param[in] dosNtPath Path of the form `\??\C:\dir\file` or
 *                      `\GLOBAL??\C:\dir\file`.
 * @param[out] volumePath The same path without the namespace and the drive.
 * @return true when the path has that form, false otherwise.
 */
bool AsVolumePath(const std::wstring& dosNtPath, std::wstring& volumePath)
{
    size_t offset = 0;
    if (StartsWithI(dosNtPath, L"\\??\\"))
    {
        offset = 4;
    }
    else if (StartsWithI(dosNtPath, L"\\GLOBAL??\\"))
    {
        offset = 10;
    }
    else
    {
        return false;
    }

    if (dosNtPath.size() < offset + 3 || !appbox::filesystem::IsDriveLetter(dosNtPath[offset]) ||
        dosNtPath[offset + 1] != L':' || dosNtPath[offset + 2] != L'\\')
    {
        return false;
    }

    /* Keep the separator which opens the first component below the volume. */
    volumePath = dosNtPath.substr(offset + 2);
    return true;
}

/**
 * @brief Undo the rebase of the upper layer.
 *
 * The upper layer rebases a view path by encoding the drive of the view as the
 * first component below its own root, so that component has to be turned back
 * into the drive of the view path.
 *
 * @param[in] layerPath Path of the upper layer, below \p upperRoot.
 * @param[in] upperRoot Root of the upper layer, in the shape of \p layerPath.
 * @param[in] namespacePrefix Namespace which opens the drive of a DOS style NT
 *                            path, empty for the form which starts at the
 *                            volume.
 * @param[in] keepDrive Whether the shape of the path carries the drive of the
 *                      volume.
 * @param[out] viewPath Path of the view, in the shape of \p layerPath.
 * @return true when the path is below the root of the upper layer.
 */
bool RebaseUpperPathToView(const std::wstring& layerPath, const std::wstring& upperRoot,
                           const std::wstring& namespacePrefix, bool keepDrive, std::wstring& viewPath)
{
    if (upperRoot.empty() || !IsBelowI(layerPath, upperRoot))
    {
        return false;
    }

    const std::wstring remainder = layerPath.substr(upperRoot.size());
    if (remainder.size() < 2 || remainder[0] != L'\\' || !appbox::filesystem::IsDriveLetter(remainder[1]))
    {
        return false;
    }

    if (remainder.size() > 2 && remainder[2] != L'\\')
    {
        return false;
    }

    /* The path is the root of the drive of the view when it has no component. */
    const std::wstring rest = remainder.size() > 2 ? remainder.substr(2) : L"\\";
    if (!keepDrive)
    {
        viewPath = rest;
        return true;
    }

    const wchar_t drive = static_cast<wchar_t>(std::towupper(remainder[1]));
    viewPath = namespacePrefix + drive + L":" + rest;
    return true;
}

/**
 * @brief Whether a view path addresses the root of a drive.
 *
 * The path can carry a "\??\" prefix, only the last two characters matter.
 *
 * @param[in] path View path without trailing separators.
 * @return true when the path is the root of a drive, for example "C:".
 */
bool IsDriveRoot(const std::wstring& path)
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
FileLayerVec MapViewPathToHost(const appbox::filesystem::ResolveFs& fs, const std::wstring& path)
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

/**
 * @brief Look one path of one layer up, component by component.
 * @param[in] path_seq Components of the mapped path, the last one is the entry.
 * @param[in] layer Index of the layer the path belongs to.
 * @param[in] has_trailing_slash Whether the caller spelled a trailing separator.
 * @param[in] ops Operations of the resolution.
 * @param[in,out] search_result Result of the search.
 * @param[in,out] resolve_result Result of the resolution.
 */
void SearchInSingleLayer(const std::vector<std::wstring>& path_seq, size_t layer, bool has_trailing_slash,
                         const appbox::filesystem::ResolveOps& ops, SearchResult& search_result,
                         appbox::filesystem::ResolveResult& resolve_result)
{
    NTSTATUS   st;
    const auto path_seq_sz = path_seq.size();
    for (size_t j = 0; j < path_seq_sz; j++)
    {
        auto comp_path = path_seq[j];

        /* Check if whiteout file exists. */
        const auto whiteout_path = appbox::filesystem::WhiteoutPathOf(comp_path);
        st = ops.query_attributes(whiteout_path, resolve_result.NameAttributes, nullptr);
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
            st = ops.query_attributes(entry_whiteout_path, resolve_result.NameAttributes, nullptr);
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
            st = ops.query_attributes(comp_path, resolve_result.NameAttributes, &parent_info);
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
            st = ops.query_attributes(opaque_path, resolve_result.NameAttributes, nullptr);
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
            st = ops.query_attributes(comp_path, resolve_result.NameAttributes, &path.fInfo);
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

/**
 * @brief Look the mapped path up in every layer of the view.
 * @param[in] path_vec Candidates of the resolution, in the order of the layers.
 * @param[in] option Lookup option.
 * @param[in] has_trailing_slash Whether the caller spelled a trailing separator.
 * @param[in] ops Operations of the resolution.
 * @param[in,out] search_result Result of the search.
 * @param[in,out] resolve_result Result of the resolution.
 */
void SearchInMultipleLayer(const FileLayerVec& path_vec, const appbox::filesystem::ResolveOption& option,
                           bool has_trailing_slash, const appbox::filesystem::ResolveOps& ops,
                           SearchResult& search_result, appbox::filesystem::ResolveResult& resolve_result)
{
    const auto path_vec_sz = path_vec.size();
    for (size_t i = 0; !search_result.whiteout_found && !search_result.opaque_found && i < path_vec_sz; ++i)
    {
        /* Split path sequence into parent directory and self. */
        const auto path_seq =
            appbox::filesystem::Sequence(path_vec[i].file_fs, path_vec[i].base_fs.size(), L"\\", true);

        /* Check each level of path. */
        SearchInSingleLayer(path_seq, i, has_trailing_slash, ops, search_result, resolve_result);

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
void ApplyIsolation(const appbox::filesystem::IsolationTable* isolation, const std::wstring& view_path,
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

/**
 * @brief Resolve a path of the view without expanding a reparse point.
 * @param[in] fs The layers of the view.
 * @param[in] vPath Path of the view to resolve.
 * @param[in] option Lookup option.
 * @param[in] isolation Isolation modes of the view, may be null.
 * @param[in] ops Operations of the resolution.
 * @return The result and the path which was resolved.
 */
ResolvePlainResult ResolvePlain(const appbox::filesystem::ResolveFs& fs, const std::wstring& vPath,
                                const appbox::filesystem::ResolveOption&  option,
                                const appbox::filesystem::IsolationTable* isolation,
                                const appbox::filesystem::ResolveOps&     ops)
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
     * would look like a missing file and every caller of the resolver would
     * report a path which the operating system resolves successfully.
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
     * process addresses the current directory of the drive, which a caller
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
    appbox::filesystem::ResolveOption search_option = option;
    if (search_option.bStopOnFirstFound && isolation != nullptr && !isolation->Empty())
    {
        appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::WriteCopy;
        appbox::FilesystemEntryKind source_kind = appbox::FilesystemEntryKind::Directory;
        if (isolation->Lookup(copy_v_path, mode, source_kind) && mode == appbox::FilesystemIsolation::Merge)
        {
            search_option.bStopOnFirstFound = false;
        }
    }

    /* For each layer, check if the file exists. */
    SearchResult search_result;
    SearchInMultipleLayer(path_vec, search_option, has_trailing_slash, ops, search_result, *resolve_result);

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

        if (resolve_result->bIsolationListed && appbox::filesystem::HidesEntry(resolve_result->isolation))
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

/**
 * @brief Read one of the counted names of the data of a reparse point.
 *
 * The name is addressed by an offset and a length relative to the name buffer
 * of the variant, both counted in bytes. The data comes from the file system of
 * a layer, so every length is validated before it is read: a record which is
 * shorter than the name it announces is refused instead of read past its end.
 *
 * @param[in] data Data of the reparse point.
 * @param[in] pathBufferOffset Offset of the name buffer inside the data.
 * @param[in] offset Offset of the name inside the name buffer.
 * @param[in] length Length of the name in bytes.
 * @param[out] name The name.
 * @return true when the name is inside the data.
 */
bool ReadReparseName(const std::vector<BYTE>& data, std::size_t pathBufferOffset, USHORT offset, USHORT length,
                     std::wstring& name)
{
    if ((length % sizeof(WCHAR)) != 0)
    {
        return false;
    }

    const std::size_t begin = pathBufferOffset + offset;
    const std::size_t end = begin + length;
    if (end < begin || end > data.size())
    {
        return false;
    }

    name.assign(reinterpret_cast<const wchar_t*>(data.data() + begin), length / sizeof(WCHAR));
    return true;
}

/**
 * @brief Split a path of the view into its root and its components.
 *
 * The root is the namespace of the object manager together with the drive, for
 * example `\??\C:`. Only a path of that shape is a path of the view: a path
 * which names no drive belongs to another isolation domain.
 *
 * @param[in] path Path of the view.
 * @param[out] root The root of the path.
 * @param[out] components The components below the root.
 * @return true when the path carries a drive.
 */
bool SplitViewPath(const std::wstring& path, std::wstring& root, std::vector<std::wstring>& components)
{
    std::size_t begin = 0;
    if (StartsWithI(path, L"\\??\\"))
    {
        begin = 4;
    }
    else if (StartsWithI(path, L"\\GLOBAL??\\"))
    {
        begin = 10;
    }
    else
    {
        return false;
    }

    if (path.size() < begin + 2 || path[begin + 1] != L':')
    {
        return false;
    }

    const wchar_t letter = path[begin];
    if (!((letter >= L'A' && letter <= L'Z') || (letter >= L'a' && letter <= L'z')))
    {
        return false;
    }

    root = path.substr(0, begin + 2);
    components.clear();

    std::size_t pos = begin + 2;
    while (pos < path.size())
    {
        while (pos < path.size() && path[pos] == L'\\')
        {
            ++pos;
        }
        if (pos >= path.size())
        {
            break;
        }

        const std::size_t separator = path.find(L'\\', pos);
        const std::size_t end = separator == std::wstring::npos ? path.size() : separator;
        components.push_back(path.substr(pos, end - pos));
        pos = end;
    }

    return true;
}

/**
 * @brief Build the path of a prefix of the components.
 * @param[in] root Root of the path.
 * @param[in] components Components of the path.
 * @param[in] count Number of components to append.
 * @return The path of the prefix.
 */
std::wstring JoinComponents(const std::wstring& root, const std::vector<std::wstring>& components, std::size_t count)
{
    std::wstring result = root;
    for (std::size_t index = 0; index < count; ++index)
    {
        result += L'\\';
        result += components[index];
    }
    return result;
}

/**
 * @brief Append the components which follow a reparse point to its target.
 * @param[in] target Path the reparse point names.
 * @param[in] components Components of the path which carries the link.
 * @param[in] from Index of the first component to append.
 * @return The path of the view which the caller reaches.
 */
std::wstring JoinRemainder(const std::wstring& target, const std::vector<std::wstring>& components, std::size_t from)
{
    if (from >= components.size())
    {
        return target;
    }

    std::wstring result = target;
    while (!result.empty() && result.back() == L'\\')
    {
        result.pop_back();
    }

    for (std::size_t index = from; index < components.size(); ++index)
    {
        result += L'\\';
        result += components[index];
    }
    return result;
}

/**
 * @brief Remove the components which name the current or the parent directory.
 * @param[in] path Path of the view.
 * @param[out] normalized Path without a `.` or a `..` component.
 * @return `STATUS_SUCCESS`, or a failure when the path leaves its drive.
 */
NTSTATUS NormalizeViewPath(const std::wstring& path, std::wstring& normalized)
{
    std::wstring              root;
    std::vector<std::wstring> components;
    if (!SplitViewPath(path, root, components))
    {
        return STATUS_REPARSE_POINT_NOT_RESOLVED;
    }

    std::vector<std::wstring> collapsed;
    collapsed.reserve(components.size());
    for (const auto& component : components)
    {
        if (component.empty() || component == L".")
        {
            continue;
        }

        if (component == L"..")
        {
            /* The target leaves the drive of the view, which no layer holds. */
            if (collapsed.empty())
            {
                return STATUS_REPARSE_POINT_NOT_RESOLVED;
            }
            collapsed.pop_back();
            continue;
        }

        collapsed.push_back(component);
    }

    if (collapsed.empty())
    {
        normalized = root + L"\\";
        return STATUS_SUCCESS;
    }

    normalized = JoinComponents(root, collapsed, collapsed.size());
    return STATUS_SUCCESS;
}

/**
 * @brief Resolve one reparse point of a path of the view.
 *
 * The components are walked from the root, so a component is looked up without
 * an unexpanded link above it: the prefix which is inspected is the entry of
 * the query, which the file system does not follow. The first component which
 * is a reparse point of a redirecting tag ends the walk and its target is
 * appended with the components behind it.
 *
 * @param[in] fs The layers of the view.
 * @param[in] viewPath Path of the view to expand.
 * @param[in] followFinal Whether the entry the path names may be expanded.
 * @param[in] nameAttributes Lookup attributes of the call.
 * @param[in] isolation Isolation modes of the virtual filesystem, may be null.
 * @param[in] ops Operations of the resolution.
 * @param[out] expanded Path of the view which the caller reaches, the path
 *                      itself when it carries no redirecting reparse point.
 * @return `STATUS_SUCCESS` or the failure of the view.
 */
NTSTATUS ExpandFirstReparsePoint(const appbox::filesystem::ResolveFs& fs, const std::wstring& viewPath,
                                 bool followFinal, ULONG nameAttributes,
                                 const appbox::filesystem::IsolationTable* isolation,
                                 const appbox::filesystem::ResolveOps& ops, std::wstring& expanded)
{
    std::wstring              root;
    std::vector<std::wstring> components;
    if (!SplitViewPath(viewPath, root, components))
    {
        expanded = viewPath;
        return STATUS_SUCCESS;
    }

    for (std::size_t index = 0; index < components.size(); ++index)
    {
        if (index + 1 == components.size() && !followFinal)
        {
            break;
        }

        const std::wstring prefix = JoinComponents(root, components, index + 1);

        appbox::filesystem::ResolveOption option;
        option.bStopOnFirstFound = true;
        option.NameAttributes = nameAttributes;
        option.reparseFollow = appbox::filesystem::ReparseFollowMode::None;

        auto probe = appbox::filesystem::ResolveCore(fs, prefix, option, isolation, ops);
        if (probe->status != appbox::filesystem::ResolveResult::Status::Exists || probe->hPath.empty())
        {
            /* No visible layer holds the component, so nothing hangs below it. */
            break;
        }

        const auto& hit = probe->hPath[0];
        if ((hit.fInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
        {
            continue;
        }

        std::vector<BYTE> data;
        const NTSTATUS    read = ops.read_reparse_data(hit.fPath, data);
        if (read == STATUS_NOT_A_REPARSE_POINT)
        {
            /* The entry carries the attribute without the data of a link. */
            continue;
        }
        if (!NT_SUCCESS(read))
        {
            Warn(ops, L"failed to read the reparse data of " + hit.fPath + L": " + std::to_wstring(read));
            return STATUS_REPARSE_POINT_NOT_RESOLVED;
        }
        if (!appbox::filesystem::IsRedirectingReparseTag(appbox::filesystem::ReparseTagOf(data)))
        {
            /* The tag describes the entry itself, the layer keeps following it. */
            continue;
        }

        std::wstring   target;
        const NTSTATUS translated = appbox::filesystem::ReparseTargetToViewPathCore(data, prefix, fs, target);
        if (!NT_SUCCESS(translated))
        {
            Warn(ops, L"the reparse point of " + prefix + L" names no path of the view");
            return translated;
        }

        Debug(ops, L"reparse point: " + prefix + L" -> " + target);
        expanded = JoinRemainder(target, components, index + 1);
        return STATUS_SUCCESS;
    }

    expanded = viewPath;
    return STATUS_SUCCESS;
}

} // namespace

ULONG appbox::filesystem::ReparseTagOf(const std::vector<BYTE>& data)
{
    if (data.size() < sizeof(ULONG))
    {
        return 0;
    }

    ULONG tag = 0;
    memcpy(&tag, data.data(), sizeof(tag));
    return tag;
}

bool appbox::filesystem::ReadReparseTarget(const std::vector<BYTE>& data, ReparseTarget& target)
{
    /* The four fields which count the names have to be inside the data. */
    if (data.size() < kReparseHeaderSize + 4 * sizeof(USHORT))
    {
        return false;
    }

    const auto* buffer = reinterpret_cast<const REPARSE_DATA_BUFFER*>(data.data());

    USHORT      substituteOffset = 0;
    USHORT      substituteLength = 0;
    USHORT      printOffset = 0;
    USHORT      printLength = 0;
    std::size_t pathBufferOffset = 0;

    if (buffer->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT)
    {
        const auto& variant = buffer->ReparseBuffer.MountPointReparseBuffer;
        substituteOffset = variant.SubstituteNameOffset;
        substituteLength = variant.SubstituteNameLength;
        printOffset = variant.PrintNameOffset;
        printLength = variant.PrintNameLength;
        pathBufferOffset = kMountPointPathBufferOffset;
    }
    else if (buffer->ReparseTag == IO_REPARSE_TAG_SYMLINK)
    {
        if (data.size() < kSymbolicLinkPathBufferOffset)
        {
            return false;
        }

        const auto& variant = buffer->ReparseBuffer.SymbolicLinkReparseBuffer;
        substituteOffset = variant.SubstituteNameOffset;
        substituteLength = variant.SubstituteNameLength;
        printOffset = variant.PrintNameOffset;
        printLength = variant.PrintNameLength;
        pathBufferOffset = kSymbolicLinkPathBufferOffset;
        target.relative = (variant.Flags & SYMLINK_FLAG_RELATIVE) != 0;
    }
    else
    {
        return false;
    }

    return ReadReparseName(data, pathBufferOffset, substituteOffset, substituteLength, target.substitute) &&
           ReadReparseName(data, pathBufferOffset, printOffset, printLength, target.print);
}

bool appbox::filesystem::IsRedirectingReparseTag(ULONG tag)
{
    return tag == IO_REPARSE_TAG_MOUNT_POINT || tag == IO_REPARSE_TAG_SYMLINK;
}

appbox::filesystem::ResolveResult::Ptr appbox::filesystem::ResolveCore(const ResolveFs&      fs,
                                                                       const std::wstring&   view_path,
                                                                       const ResolveOption&  option,
                                                                       const IsolationTable* isolation,
                                                                       const ResolveOps&     ops)
{
    ResolvePlainResult plain = ResolvePlain(fs, view_path, option, isolation, ops);

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
    const NTSTATUS status = ExpandReparsePointsCore(fs, plain.viewPath, option.reparseFollow, option.NameAttributes,
                                                    isolation, ops, expanded);
    if (!NT_SUCCESS(status))
    {
        Warn(ops, L"failed to resolve the reparse point of " + plain.viewPath + L": " + std::to_wstring(status));
        plain.result->reparseStatus = status;
        return plain.result;
    }

    if (expanded == plain.viewPath)
    {
        /* The path carries no link which redirects the namespace. */
        return plain.result;
    }

    ResolvePlainResult resolved = ResolvePlain(fs, expanded, option, isolation, ops);
    return resolved.result;
}

NTSTATUS appbox::filesystem::ExpandReparsePointsCore(const ResolveFs& fs, const std::wstring& view_path,
                                                     ReparseFollowMode mode, ULONG name_attributes,
                                                     const IsolationTable* isolation, const ResolveOps& ops,
                                                     std::wstring& effective_view_path)
{
    if (mode == ReparseFollowMode::None)
    {
        effective_view_path = view_path;
        return STATUS_SUCCESS;
    }

    /*
     * A stream belongs to the file which carries it, so the entry the expansion
     * follows is the file and the name of the stream is appended to the path
     * the file resolves to.
     */
    const std::wstring streamName = StreamNameOf(view_path);
    std::wstring       current = EntryPathOfStream(view_path);

    for (std::size_t depth = 0; depth <= kMaxReparseDepth; ++depth)
    {
        std::wstring   expanded;
        const NTSTATUS status = ExpandFirstReparsePoint(fs, current, mode == ReparseFollowMode::All, name_attributes,
                                                        isolation, ops, expanded);
        if (!NT_SUCCESS(status))
        {
            return status;
        }

        if (expanded == current)
        {
            effective_view_path = streamName.empty() ? current : current + L":" + streamName;
            return STATUS_SUCCESS;
        }

        current = std::move(expanded);
    }

    Warn(ops, L"the chain of reparse points of " + view_path + L" is too deep");
    return STATUS_REPARSE_POINT_NOT_RESOLVED;
}

NTSTATUS appbox::filesystem::ReparseTargetToViewPathCore(const std::vector<BYTE>& data,
                                                         const std::wstring& link_view_path, const ResolveFs& fs,
                                                         std::wstring& target_view_path)
{
    ReparseTarget target;
    if (!ReadReparseTarget(data, target))
    {
        return STATUS_NOT_A_REPARSE_POINT;
    }

    if (target.substitute.empty())
    {
        return STATUS_REPARSE_POINT_NOT_RESOLVED;
    }

    if (target.relative)
    {
        /*
         * The view maps a layer by replacing the prefix of the path, so the
         * components below the link keep their shape and a target which is
         * relative to the directory of the link names the same entry in the
         * view as it does in the layer.
         */
        std::wstring joined = DirName(link_view_path);
        if (joined.empty())
        {
            return STATUS_REPARSE_POINT_NOT_RESOLVED;
        }
        if (joined.back() != L'\\')
        {
            joined += L'\\';
        }
        joined += target.substitute;

        return NormalizeViewPath(joined, target_view_path);
    }

    std::wstring   viewPath;
    const NTSTATUS status = ViewNamespaceTargetCore(target.substitute, fs, viewPath);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    return NormalizeViewPath(viewPath, target_view_path);
}

NTSTATUS appbox::filesystem::ViewNamespaceTargetCore(const std::wstring& target, const ResolveFs& fs,
                                                     std::wstring& view_target)
{
    if (target.empty())
    {
        return STATUS_REPARSE_POINT_NOT_RESOLVED;
    }

    /*
     * A caller may hold the path of a layer: the file system reports that path
     * for a handle, so a link which an application creates out of a handle it
     * holds would otherwise store the layout of the sandbox.
     */
    std::wstring viewPath;
    if (RebaseLayerPathToViewCore(fs, target, viewPath))
    {
        view_target = viewPath;
        return STATUS_SUCCESS;
    }

    /*
     * A path of the view is a DOS style path, and a device path which names a
     * local volume is one as well: the mapping turns `\Device\HarddiskVolumeX`
     * and `\??\Volume{GUID}` into the drive of the view, and refuses the
     * namespaces which belong to another isolation domain.
     */
    if (appbox::MappingAsDosNtPath(target, viewPath))
    {
        view_target = viewPath;
        return STATUS_SUCCESS;
    }

    return STATUS_REPARSE_POINT_NOT_RESOLVED;
}

bool appbox::filesystem::RebaseLayerPathToViewCore(const ResolveFs& fs, const std::wstring& layer_path,
                                                   std::wstring& view_path)
{
    if (layer_path.empty())
    {
        return false;
    }

    /*
     * The layer path of a DOS style NT path carries the namespace and the drive
     * of the layer, so the roots of the configuration are matched directly.
     */
    if (!DosNtNamespace(layer_path).empty())
    {
        if (RebaseUpperPathToView(layer_path, fs.fs_upper, DosNtNamespace(fs.fs_upper), true, view_path))
        {
            return true;
        }

        for (const auto& mapping : fs.fs_lower)
        {
            if (mapping.host_nt_path.empty() || !IsBelowI(layer_path, mapping.host_nt_path))
            {
                continue;
            }

            view_path = mapping.mapped_nt_path + layer_path.substr(mapping.host_nt_path.size());
            return true;
        }

        return false;
    }

    /*
     * The name of a handle starts at the root of its volume, so the same
     * comparison runs on the two paths without their namespace and drive.
     */
    std::wstring upperPath;
    if (AsVolumePath(fs.fs_upper, upperPath) && RebaseUpperPathToView(layer_path, upperPath, L"", false, view_path))
    {
        return true;
    }

    for (const auto& mapping : fs.fs_lower)
    {
        std::wstring lowerPath;
        std::wstring mappedPath;
        if (!AsVolumePath(mapping.host_nt_path, lowerPath) || !AsVolumePath(mapping.mapped_nt_path, mappedPath))
        {
            continue;
        }
        if (lowerPath.empty() || !IsBelowI(layer_path, lowerPath))
        {
            continue;
        }

        const std::wstring remainder = layer_path.substr(lowerPath.size());
        if (mappedPath == L"\\")
        {
            /* The layer is mounted at the root of the volume of the view. */
            view_path = remainder.empty() ? L"\\" : remainder;
        }
        else
        {
            view_path = mappedPath + remainder;
        }
        return true;
    }

    return false;
}
