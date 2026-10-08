#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/ConvertToFullNtPath.hpp"
#include "utils/Log.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "Resolve.hpp"
#include "QueryPath.hpp"

appbox::filesystem::QueryPathResult appbox::filesystem::QueryPathFromResolve(const ResolveResult& resolve)
{
    QueryPathResult result;

    if (!resolve.bParentExist)
    {
        result.outcome = QueryPathResult::Outcome::NotFound;
        result.status = STATUS_OBJECT_PATH_NOT_FOUND;
        return result;
    }
    if (resolve.status != ResolveResult::Status::Exists)
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
    result.layerPath = resolve.hPath[0].fPath;
    return result;
}

appbox::filesystem::QueryPathResult appbox::filesystem::ResolveViewPath(const std::wstring& viewPath,
                                                                        ULONG nameAttributes, bool stopOnFirstFound)
{
    ResolveOption resolve_option;
    resolve_option.NameAttributes = nameAttributes;
    resolve_option.bStopOnFirstFound = stopOnFirstFound;

    auto resolve_result = Resolve(viewPath, resolve_option);
    LOG_T("resolve: {}", appbox::DumpJson(nlohmann::json(*resolve_result)));

    return QueryPathFromResolve(*resolve_result);
}

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

    return ResolveViewPath(native_fs_path, ObjectAttributes->Attributes);
}
