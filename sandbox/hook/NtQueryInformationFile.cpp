#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "NtQueryInformationFile.hpp"

T_NtQueryInformationFile sys_NtQueryInformationFile = nullptr;

static nlohmann::json NtQueryInformationFileLogParam(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
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
static appbox::LoggerF logger("NtQueryInformationFile", NtQueryInformationFileLogParam);

static NTSTATUS Hook_NtQueryInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                            ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    logger.Log(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
    return sys_NtQueryInformationFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
}

static void LoadNtQueryInformationFile()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtQueryInformationFile");
    sys_NtQueryInformationFile = reinterpret_cast<T_NtQueryInformationFile>(addr);
}

appbox::HookRecord appbox::HookNtQueryInformationFile = {
    "NtQueryInformationFile",
    LoadNtQueryInformationFile,
    (void**)&sys_NtQueryInformationFile,
    Hook_NtQueryInformationFile,
};
