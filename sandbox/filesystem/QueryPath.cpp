#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/ConvertToFullNtPath.hpp"
#include "utils/Log.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "Resolve.hpp"
#include "QueryPath.hpp"

appbox::filesystem::QueryPathResult appbox::filesystem::ResolveQueryPath(const POBJECT_ATTRIBUTES ObjectAttributes)
{
    QueryPathResult result;

    if (ObjectAttributes == nullptr)
    {
        return result;
    }

    /* Extract the view path of the query. */
    std::wstring native_fs_nt_path;
    if (appbox::ConvertToFullNtPath(ObjectAttributes, 0, native_fs_nt_path) != 0)
    {
        return result;
    }

    std::wstring native_fs_path;
    if (!appbox::MappingAsDosNtPath(native_fs_nt_path, native_fs_path))
    {
        return result;
    }

    /* Resolve the view path against the layers of the view. */
    ResolveOption resolve_option;
    resolve_option.NameAttributes = ObjectAttributes->Attributes;

    auto resolve_result = Resolve(native_fs_path, resolve_option);
    LOG_T("resolve: {}", appbox::DumpJson(nlohmann::json(*resolve_result)));

    if (!resolve_result->bParentExist)
    {
        result.outcome = QueryPathResult::Outcome::NotFound;
        result.status = STATUS_OBJECT_PATH_NOT_FOUND;
        return result;
    }
    if (resolve_result->status != ResolveResult::Status::Exists)
    {
        result.outcome = QueryPathResult::Outcome::NotFound;
        result.status = STATUS_OBJECT_NAME_NOT_FOUND;
        return result;
    }

    /*
     * The isolation already dropped the hits of the layers a mode hides, so
     * the first hit is the layer the view reports the entry from: the upper
     * layer first, then the lower layers and the host layer last.
     */
    result.outcome = QueryPathResult::Outcome::Found;
    result.layerPath = resolve_result->hPath[0].fPath;
    return result;
}
