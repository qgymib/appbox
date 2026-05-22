#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "NtQueryDirectoryFileEx.hpp"
#include "__init__.hpp"
#include <detours.h>

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
    return sys_NtQueryDirectoryFileEx(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length,
                                      FileInformationClass, QueryFlags, FileName);
}

void appbox::InjectNtQueryDirectoryFileEx()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtQueryAttributesFile");
    sys_NtQueryDirectoryFileEx = reinterpret_cast<T_NtQueryDirectoryFileEx>(addr);

    DetourAttach(&sys_NtQueryDirectoryFileEx, Hook_NtQueryDirectoryFileEx);
}
