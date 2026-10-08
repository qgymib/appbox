#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "filesystem/QueryPath.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "NtQueryFullAttributesFile.hpp"

T_NtQueryFullAttributesFile sys_NtQueryFullAttributesFile = nullptr;

static nlohmann::json NtQueryFullAttributesFileLogParam(POBJECT_ATTRIBUTES             ObjectAttributes,
                                                        PFILE_NETWORK_OPEN_INFORMATION FileInformation)
{
    nlohmann::json param;
    param["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    param["FileInformation"] = appbox::PointerToString(FileInformation);
    return param;
}

static appbox::LoggerF logger("NtQueryFullAttributesFile", NtQueryFullAttributesFileLogParam);

static NTSTATUS Hook_NtQueryFullAttributesFile(POBJECT_ATTRIBUTES             ObjectAttributes,
                                               PFILE_NETWORK_OPEN_INFORMATION FileInformation)
{
    logger.Log(ObjectAttributes, FileInformation);

    const auto query = appbox::filesystem::ResolveQueryPath(ObjectAttributes);
    if (query.outcome == appbox::filesystem::QueryPathResult::Outcome::Forward)
    {
        return sys_NtQueryFullAttributesFile(ObjectAttributes, FileInformation);
    }
    if (query.outcome == appbox::filesystem::QueryPathResult::Outcome::NotFound)
    {
        return query.status;
    }

    /* Query the first layer which holds the object. */
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING    us_path;
    sys_RtlInitUnicodeString(&us_path, query.layerPath.c_str());
    InitializeObjectAttributes(&oa, &us_path, ObjectAttributes->Attributes, nullptr, nullptr);

    return sys_NtQueryFullAttributesFile(&oa, FileInformation);
}

static void LoadNtQueryFullAttributesFile()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtQueryFullAttributesFile");
    sys_NtQueryFullAttributesFile = reinterpret_cast<T_NtQueryFullAttributesFile>(addr);
}

appbox::HookRecord appbox::HookNtQueryFullAttributesFile = {
    "NtQueryFullAttributesFile",
    LoadNtQueryFullAttributesFile,
    (void**)&sys_NtQueryFullAttributesFile,
    Hook_NtQueryFullAttributesFile,
};
