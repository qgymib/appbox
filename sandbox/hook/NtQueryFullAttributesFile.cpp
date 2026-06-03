#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
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
    return sys_NtQueryFullAttributesFile(ObjectAttributes, FileInformation);
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
