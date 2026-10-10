#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/ConvertToFullNtPath.hpp"
#include "utils/Log.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "filesystem/MarkerName.hpp"
#include "Resolve.hpp"
#include "QueryPath.hpp"

appbox::filesystem::QueryPathResult appbox::filesystem::QueryPathFromResolve(const ResolveResult& resolve)
{
    QueryPathResult result;

    /*
     * A reparse point which the view could not resolve fails the call: the name
     * reaches an object the view never decided about, so the caller must not
     * let the layer the link belongs to follow it.
     */
    if (!NT_SUCCESS(resolve.reparseStatus))
    {
        result.outcome = QueryPathResult::Outcome::NotFound;
        result.status = resolve.reparseStatus;
        return result;
    }

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
    QueryPathResult result;

    /*
     * The names of the markers are reserved: a path which carries one names
     * the view rather than an entry it holds, see `MarkerName.hpp`. A query
     * reports the entry as missing, like an entry the view hides, and reports
     * a missing path for a name which hangs below a reserved component.
     */
    const NTSTATUS marker_status = ReservedMarkerNameFailure(viewPath, STATUS_OBJECT_NAME_NOT_FOUND);
    if (!NT_SUCCESS(marker_status))
    {
        result.outcome = QueryPathResult::Outcome::NotFound;
        result.status = marker_status;
        return result;
    }

    ResolveOption resolve_option;
    resolve_option.NameAttributes = nameAttributes;
    resolve_option.bStopOnFirstFound = stopOnFirstFound;

    /*
     * A query reports the attributes of the entry the caller names, so the
     * entry itself is not followed: a name which addresses a link reports the
     * link, like the file system does for the same query. The components above
     * the entry are resolved by the view all the same.
     */
    resolve_option.reparseFollow = ReparseFollowMode::Parent;

    auto resolve_result = Resolve(viewPath, resolve_option);
    LOG_T("resolve: {}", appbox::DumpJson(nlohmann::json(*resolve_result)));

    /*
     * The view resolves the reparse points of the name itself, so the path the
     * query reaches may differ from the name the caller spelled: a link which
     * names one of the markers of the view is refused the same way.
     */
    const NTSTATUS expanded_status = ReservedMarkerNameFailure(resolve_result->viewPath, STATUS_OBJECT_NAME_NOT_FOUND);
    if (!NT_SUCCESS(expanded_status))
    {
        result.outcome = QueryPathResult::Outcome::NotFound;
        result.status = expanded_status;
        return result;
    }

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
