#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "filesystem/DirectoryMerge.hpp"
#include "NtQueryDirectoryFileEx.hpp"

T_NtQueryDirectoryFileEx sys_NtQueryDirectoryFileEx = nullptr;

static nlohmann::json NtQueryDirectoryFileExLogParam(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                                     PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock,
                                                     PVOID FileInformation, ULONG Length,
                                                     FILE_INFORMATION_CLASS FileInformationClass, ULONG QueryFlags,
                                                     PUNICODE_STRING FileName)
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
    param["QueryFlags"] = QueryFlags;
    param["FileName"] = appbox::ToJson(FileName);
    return param;
}
static appbox::LoggerF logger("NtQueryDirectoryFileEx", NtQueryDirectoryFileExLogParam);

static NTSTATUS Hook_NtQueryDirectoryFileEx(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                            PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                            ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, ULONG QueryFlags,
                                            PUNICODE_STRING FileName)
{
    logger.Log(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass,
               QueryFlags, FileName);

    /*
     * The view answers the enumeration of a directory it holds, whatever handle
     * the call names: the merge answers a class which carries the name of an
     * entry and refuses every other class instead of forwarding it, because the
     * answer of the layer the handle was opened with would show the entries a
     * whiteout, an opaque marker or the isolation hides, and the markers
     * themselves. The merge adopts a handle the sandbox did not open, which is
     * the handle a process inherited or duplicated, so an enumeration through
     * such a handle reports the view as well. A handle which denotes no
     * directory of the view is not part of it, so its call is forwarded
     * unchanged.
     */
    bool           handled = false;
    const NTSTATUS status = appbox::filesystem::QueryDirectoryInformation(
        FileHandle, IoStatusBlock, FileInformation, Length, QueryFlags, FileName, FileInformationClass, true, handled);
    if (handled)
    {
        return status;
    }

    return sys_NtQueryDirectoryFileEx(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length,
                                      FileInformationClass, QueryFlags, FileName);
}

static void LoadNtQueryDirectoryFileEx()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtQueryDirectoryFileEx");
    sys_NtQueryDirectoryFileEx = reinterpret_cast<T_NtQueryDirectoryFileEx>(addr);
}

appbox::HookRecord appbox::HookNtQueryDirectoryFileEx = {
    "NtQueryDirectoryFileEx",
    LoadNtQueryDirectoryFileEx,
    (void**)&sys_NtQueryDirectoryFileEx,
    Hook_NtQueryDirectoryFileEx,
};
