#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ConvertToFullNtPath.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "filesystem/Resolve.hpp"
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

    /* Extract the view path of the query. */
    std::wstring native_fs_nt_path;
    if (appbox::ConvertToFullNtPath(ObjectAttributes, 0, native_fs_nt_path) != 0)
    {
        return sys_NtQueryAttributesFile(ObjectAttributes, FileInformation);
    }

    std::wstring native_fs_path;
    if (!appbox::MappingAsDosNtPath(native_fs_nt_path, native_fs_path))
    {
        return sys_NtQueryAttributesFile(ObjectAttributes, FileInformation);
    }

    /* Resolve path in sandbox. */
    appbox::filesystem::ResolveOption resolve_option;
    resolve_option.NameAttributes = ObjectAttributes->Attributes;

    auto resolve_result = appbox::filesystem::Resolve(native_fs_path, resolve_option);
    LOG_T("resolve: {}", appbox::DumpJson(nlohmann::json(*resolve_result)));

    if (!resolve_result->bParentExist)
    {
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }
    if (resolve_result->status != appbox::filesystem::ResolveResult::Status::Exists)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    /* Query the first layer which holds the object. */
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING    us_path;
    sys_RtlInitUnicodeString(&us_path, resolve_result->hPath[0].fPath.c_str());
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
