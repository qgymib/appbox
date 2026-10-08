#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "filesystem/QueryPath.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "hook/NtQueryInformationByName.hpp"

T_NtQueryInformationByName sys_NtQueryInformationByName = nullptr;

static nlohmann::json NtQueryInformationByNameLogParam(POBJECT_ATTRIBUTES ObjectAttributes,
                                                       PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                                       ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    nlohmann::json param;
    param["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    param["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    param["FileInformation"] = appbox::PointerToString(FileInformation);
    param["Length"] = Length;
    param["FileInformationClass"] = FileInformationClass;
    return param;
}

static appbox::LoggerF logger("NtQueryInformationByName", NtQueryInformationByNameLogParam);

static NTSTATUS Hook_NtQueryInformationByName(POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock,
                                              PVOID FileInformation, ULONG Length,
                                              FILE_INFORMATION_CLASS FileInformationClass)
{
    logger.Log(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);

    const auto query = appbox::filesystem::ResolveQueryPath(ObjectAttributes);
    if (query.outcome == appbox::filesystem::QueryPathResult::Outcome::Forward)
    {
        return sys_NtQueryInformationByName(ObjectAttributes, IoStatusBlock, FileInformation, Length,
                                            FileInformationClass);
    }
    if (query.outcome == appbox::filesystem::QueryPathResult::Outcome::NotFound)
    {
        return query.status;
    }

    /*
     * Query the first layer which holds the object. The information classes the
     * entry point accepts carry no name of their own (`FileStatInformation`,
     * `FileStatLxInformation`, `FileCaseSensitiveInformation`), so redirecting
     * the name of the call is enough: the caller receives the attributes of the
     * entry the view reports.
     */
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING    us_path;
    sys_RtlInitUnicodeString(&us_path, query.layerPath.c_str());
    InitializeObjectAttributes(&oa, &us_path, ObjectAttributes->Attributes, nullptr, nullptr);

    return sys_NtQueryInformationByName(&oa, IoStatusBlock, FileInformation, Length, FileInformationClass);
}

static void LoadNtQueryInformationByName()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtQueryInformationByName");
    sys_NtQueryInformationByName = reinterpret_cast<T_NtQueryInformationByName>(addr);
}

appbox::HookRecord appbox::HookNtQueryInformationByName = {
    "NtQueryInformationByName",
    LoadNtQueryInformationByName,
    (void**)&sys_NtQueryInformationByName,
    Hook_NtQueryInformationByName,
};
