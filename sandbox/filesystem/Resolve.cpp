#include "utils/CheckPathExist.hpp"
#include "utils/Log.hpp"
#include "ReparsePoint.hpp"
#include "ResolveCore.hpp"
#include "ResolveOps.hpp"
#include "Sandbox.hpp"
#include "WString.hpp"
#include "__init__.hpp"
#include "Resolve.hpp"

namespace
{

/**
 * @brief Report a diagnostic of the resolution into the log of the process.
 * @param[in] message Message to report.
 */
void SandboxLogWarn(const std::wstring& message)
{
    LOG_W(L"{}", message);
}

/**
 * @brief Report a trace of the resolution into the log of the process.
 * @param[in] message Message to report.
 */
void SandboxLogDebug(const std::wstring& message)
{
    LOG_D(L"{}", message);
}

} // namespace

const appbox::filesystem::ResolveOps& appbox::filesystem::SandboxResolveOps()
{
    /*
     * The lookups run through the entry points the sandbox saved before it
     * attached its hooks, so a path of a layer is answered by the file system
     * itself instead of the view again. The structure never changes, so it is
     * built once.
     */
    static const ResolveOps ops = {
        appbox::CheckPathExist,
        appbox::filesystem::ReadReparseData,
        SandboxLogWarn,
        SandboxLogDebug,
    };

    return ops;
}

appbox::filesystem::ResolveResult::Ptr appbox::filesystem::ResolveFull(const ResolveFs& fs, const std::wstring& vPath,
                                                                       const ResolveOption&  option,
                                                                       const IsolationTable* isolation)
{
    return ResolveCore(fs, vPath, option, isolation, SandboxResolveOps());
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
    j["bSandboxHolds"] = r.bSandboxHolds;

    j["bIsolationListed"] = r.bIsolationListed;
    j["bIsolationMasked"] = r.bIsolationMasked;
    j["isolation"] = appbox::filesystem_isolation::IsolationToken(r.isolation);
    j["isolationSource"] = appbox::filesystem_isolation::EntryKindToken(r.isolationSource);

    j["viewPath"] = appbox::WideToUTF8(r.viewPath);
    j["reparseStatus"] = r.reparseStatus;
}
