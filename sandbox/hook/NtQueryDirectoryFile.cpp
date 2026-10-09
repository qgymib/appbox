#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/HandleInfo.hpp"
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
     * A handle the view opened is answered by the view, whatever the
     * information class: the merge answers a class which carries the name of an
     * entry, and refuses every other class instead of forwarding it, because
     * the answer of the layer the handle was opened with would show the entries
     * a whiteout, an opaque marker or the isolation hides, and the markers
     * themselves. A handle the view did not open is not part of the view, so
     * its call is forwarded unchanged.
     *
     * The plain entry point reports a directory of the view like the extended
     * one: both share the merge of the layers, which is what keeps the two
     * enumerations of the same handle consistent.
     */
    if (appbox::HandleInfo::Find(FileHandle) != nullptr)
    {
        ULONG query_flags = 0;
        if (ReturnSingleEntry)
        {
            query_flags |= appbox::filesystem::kQueryReturnSingleEntry;
        }
        if (RestartScan)
        {
            query_flags |= appbox::filesystem::kQueryRestartScan;
        }

        return appbox::filesystem::QueryDirectoryInformation(FileHandle, IoStatusBlock, FileInformation, Length,
                                                             query_flags, FileName, FileInformationClass, false);
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
