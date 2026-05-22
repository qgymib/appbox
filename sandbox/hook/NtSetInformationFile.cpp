#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "NtSetInformationFile.hpp"
#include "__init__.hpp"
#include <detours.h>

T_NtSetInformationFile sys_NtSetInformationFile = nullptr;

static nlohmann::json NtSetInformationFileLogParam(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
                                                   PVOID FileInformation, ULONG Length,
                                                   FILE_INFORMATION_CLASS FileInformationClass)
{
    nlohmann::json param;
    param["FileHandle"] = appbox::PointerToString(FileHandle);
    param["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    param["FileInformation"] = appbox::PointerToString(FileInformation);
    param["Length"] = Length;
    param["FileInformationClass"] = FileInformationClass;
    return param;
}
static appbox::LoggerF logger("NtSetInformationFile", NtSetInformationFileLogParam);

static NTSTATUS Hook_NtSetInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                          ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    logger.Log(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
    return sys_NtSetInformationFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
}

void appbox::InjectNtSetInformationFile()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtSetInformationFile");
    sys_NtSetInformationFile = reinterpret_cast<T_NtSetInformationFile>(addr);

    DetourAttach(&sys_NtSetInformationFile, Hook_NtSetInformationFile);
}
