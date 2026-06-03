#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "NtQueryDirectoryFile.hpp"

T_NtQueryDirectoryFile sys_NtQueryDirectoryFile = nullptr;

static nlohmann::json NtQueryDirectoryFileLogParam(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                                   PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock,
                                                   PVOID FileInformation, ULONG Length,
                                                   FILE_INFORMATION_CLASS FileInformationClass,
                                                   BOOLEAN ReturnSingleEntry, PUNICODE_STRING FileName,
                                                   BOOLEAN RestartScan)
{
    nlohmann::json param;
    param["FileHandle"] = appbox::PointerToString(FileHandle);
    param["Event"] = appbox::PointerToString(Event);
    param["ApcRoutine"] = appbox::PointerToString(ApcRoutine);
    param["ApcContext"] = appbox::PointerToString(ApcContext);
    param["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    param["FileInformation"] = appbox::PointerToString(FileInformation);
    param["Length"] = Length;
    param["FileInformationClass"] = FileInformationClass;
    param["ReturnSingleEntry"] = ReturnSingleEntry;
    param["FileName"] = appbox::ToJson(FileName);
    param["RestartScan"] = RestartScan;
    return param;
}
static appbox::LoggerF logger("NtQueryDirectoryFile", NtQueryDirectoryFileLogParam);

static NTSTATUS Hook_NtQueryDirectoryFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
                                          PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length,
                                          FILE_INFORMATION_CLASS FileInformationClass, BOOLEAN ReturnSingleEntry,
                                          PUNICODE_STRING FileName, BOOLEAN RestartScan)
{
    logger.Log(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass,
               ReturnSingleEntry, FileName, RestartScan);
    return sys_NtQueryDirectoryFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length,
                                    FileInformationClass, ReturnSingleEntry, FileName, RestartScan);
}

static void LoadNtQueryDirectoryFile()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtQueryDirectoryFile");
    sys_NtQueryDirectoryFile = reinterpret_cast<T_NtQueryDirectoryFile>(addr);
}

appbox::HookRecord appbox::HookNtQueryDirectoryFile = {
    "NtQueryDirectoryFile",
    LoadNtQueryDirectoryFile,
    (void**)&sys_NtQueryDirectoryFile,
    Hook_NtQueryDirectoryFile,
};
