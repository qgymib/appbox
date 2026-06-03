#include <vector>
#include "filesystem/Sequence.hpp"
#include "utils/CheckPathExist.hpp"
#include "utils/MappingAsSandboxNtPath.hpp"
#include "utils/Defines.hpp"
#include "Sandbox.hpp"
#include "WString.hpp"
#include "__init__.hpp"
#include "Resolve.hpp"

struct SearchResult
{
    bool whiteout_found = false;
    bool opaque_found = false;
};

struct FileLayer
{
    std::wstring base_fs; /* Basis filesystem path. */
    std::wstring file_fs; /* File path based on basis filesystem. */
};
typedef std::vector<FileLayer> FileLayerVec;

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

static void SearchInSingleLayer(const std::vector<std::wstring>& path_seq, bool has_trailing_slash,
                                SearchResult& search_result, appbox::filesystem::ResolveResult& resolve_result)
{
    NTSTATUS   st;
    const auto path_seq_sz = path_seq.size();
    for (size_t j = 0; j < path_seq_sz; j++)
    {
        auto comp_path = path_seq[j];

        /* Check if whiteout file exists. */
        const auto whiteout_path = comp_path + APPBOX_SANDBOX_WHITEOUT_SUFFIX_W;
        st = appbox::CheckPathExist(whiteout_path, resolve_result.NameAttributes, nullptr);
        if (NT_SUCCESS(st))
        {
            search_result.whiteout_found = true;
            resolve_result.whiteoutPath = whiteout_path;
            return;
        }

        /* Check parent path. */
        if (j != path_seq_sz - 1)
        {
            /* If path is drive letter, add backslash. */
            if (j == 0 && comp_path.back() == L':')
            {
                comp_path += L"\\";
            }

            st = appbox::CheckPathExist(comp_path, resolve_result.NameAttributes, nullptr);
            if (NT_SUCCESS(st))
            { /* All parents must exists. */
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
            const auto opaque_path = comp_path + L"\\" + APPBOX_SANDBOX_OPAQUE_NAME_W;
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
            st = appbox::CheckPathExist(comp_path, resolve_result.NameAttributes, &path.fInfo);
            /* For file self, check if exists. */
            if (NT_SUCCESS(st))
            {
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
        SearchInSingleLayer(path_seq, has_trailing_slash, search_result, resolve_result);

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

appbox::filesystem::ResolveResult appbox::filesystem::ResolveFull(const ResolveFs& fs, const std::wstring& vPath,
                                                                  const appbox::filesystem::ResolveOption& option)
{
    appbox::filesystem::ResolveResult resolve_result;
    resolve_result.status = appbox::filesystem::ResolveResult::Status::Exists;
    resolve_result.NameAttributes = option.NameAttributes;

    /* Remove trailing slash */
    auto copy_v_path = vPath;
    bool has_trailing_slash = false;
    while (copy_v_path.back() == L'\\')
    {
        has_trailing_slash = true;
        copy_v_path.pop_back();
    }

    /* Generate host path sequence for upper / lower / host filesystem. */
    const auto path_vec = MapViewPathToHost(fs, copy_v_path);
    resolve_result.uPath = path_vec[0].file_fs + (has_trailing_slash ? L"\\" : L"");
    resolve_result.uPathBaseSize = path_vec[0].base_fs.size();

    /* For each layer, check if the file exists. */
    SearchResult search_result;
    SearchInMultipleLayer(path_vec, option, has_trailing_slash, search_result, resolve_result);

    /* Fix status. */
    if (resolve_result.hPath.empty())
    {
        if (!resolve_result.whiteoutPath.empty())
        {
            resolve_result.status = appbox::filesystem::ResolveResult::Status::HiddenByWhiteout;
            return resolve_result;
        }

        if (search_result.opaque_found)
        {
            resolve_result.status = appbox::filesystem::ResolveResult::Status::BlockedByOpaque;
            return resolve_result;
        }

        resolve_result.status = appbox::filesystem::ResolveResult::Status::NotFound;
    }

    return resolve_result;
}

appbox::filesystem::ResolveResult appbox::filesystem::Resolve(const std::wstring& vPath, const ResolveOption& option)
{
    return ResolveFull(appbox::sandbox->fs, vPath, option);
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
}
