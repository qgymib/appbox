#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "filesystem/QueryPath.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "NtQueryAttributesFile.hpp"

static nlohmann::json NtQueryAttributesFileLogParam(POBJECT_ATTRIBUTES      ObjectAttributes,
                                                    PFILE_BASIC_INFORMATION FileInformation)
{
    nlohmann::json json;
    json["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    json["FileInformation"] = appbox::PointerToString(FileInformation);
    return json;
}

T_NtQueryAttributesFile sys_NtQueryAttributesFile = nullptr;
static appbox::LoggerF  logger("NtQueryAttributesFile", NtQueryAttributesFileLogParam);

static NTSTATUS Hook_NtQueryAttributesFile(POBJECT_ATTRIBUTES ObjectAttributes, PFILE_BASIC_INFORMATION FileInformation)
{
    logger.Log(ObjectAttributes, FileInformation);

    const auto query = appbox::filesystem::ResolveQueryPath(ObjectAttributes);
    if (query.outcome == appbox::filesystem::QueryPathResult::Outcome::Forward)
    {
        return sys_NtQueryAttributesFile(ObjectAttributes, FileInformation);
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

    return sys_NtQueryAttributesFile(&oa, FileInformation);
}

static void LoadNtQueryAttributesFile()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtQueryAttributesFile");
    sys_NtQueryAttributesFile = reinterpret_cast<T_NtQueryAttributesFile>(addr);
}

appbox::HookRecord appbox::HookNtQueryAttributesFile = {
    "NtQueryAttributesFile",
    LoadNtQueryAttributesFile,
    (void**)&sys_NtQueryAttributesFile,
    Hook_NtQueryAttributesFile,
};
