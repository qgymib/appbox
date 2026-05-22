#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "NtQueryAttributesFile.hpp"
#include "__init__.hpp"
#include <detours.h>

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

    // TODO
    return sys_NtQueryAttributesFile(ObjectAttributes, FileInformation);
}

void appbox::InjectNtQueryAttributesFile()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtQueryAttributesFile");
    sys_NtQueryAttributesFile = reinterpret_cast<T_NtQueryAttributesFile>(addr);

    DetourAttach(&sys_NtQueryAttributesFile, Hook_NtQueryAttributesFile);
}
