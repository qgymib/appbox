#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "filesystem/DirectoryMerge.hpp"
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

    /*
     * The view answers the enumeration of a directory it holds, whatever handle
     * the call names: the merge answers a class which carries the name of an
     * entry, and refuses every other class instead of forwarding it, because
     * the answer of the layer the handle was opened with would show the entries
     * a whiteout, an opaque marker or the isolation hides, and the markers
     * themselves. The merge adopts a handle the sandbox did not open, which is
     * the handle a process inherited or duplicated, so an enumeration through
     * such a handle reports the view as well. A handle which denotes no
     * directory of the view is not part of it, so its call is forwarded
     * unchanged.
     *
     * The plain entry point reports a directory of the view like the extended
     * one: both share the merge of the layers, which is what keeps the two
     * enumerations of the same handle consistent.
     */
    ULONG query_flags = 0;
    if (ReturnSingleEntry)
    {
        query_flags |= appbox::filesystem::kQueryReturnSingleEntry;
    }
    if (RestartScan)
    {
        query_flags |= appbox::filesystem::kQueryRestartScan;
    }

    bool           handled = false;
    const NTSTATUS status =
        appbox::filesystem::QueryDirectoryInformation(FileHandle, IoStatusBlock, FileInformation, Length, query_flags,
                                                      FileName, FileInformationClass, false, handled);
    if (handled)
    {
        return status;
    }

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
