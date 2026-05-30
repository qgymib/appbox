#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "hook/NtQueryInformationByName.hpp"
#include "__init__.hpp"
#include <detours.h>

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
    return sys_NtQueryInformationByName(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);
}

void appbox::AttachNtQueryInformationByName()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtQueryInformationByName");
    sys_NtQueryInformationByName = reinterpret_cast<T_NtQueryInformationByName>(addr);

    DetourAttach(&sys_NtQueryInformationByName, Hook_NtQueryInformationByName);
}

void appbox::DetachNtQueryInformationByName()
{
    DetourDetach(&sys_NtQueryInformationByName, Hook_NtQueryInformationByName);
}
