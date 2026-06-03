#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "NtFsControlFile.hpp"

T_NtFsControlFile sys_NtFsControlFile = nullptr;

static nlohmann::json NtFsControlFileLogParam(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                              PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, ULONG FsControlCode,
                                              PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer,
                                              ULONG OutputBufferLength)
{
    nlohmann::json param;
    param["FileHandle"] = appbox::PointerToString(FileHandle);
    param["Event"] = appbox::PointerToString(Event);
    param["ApcRoutine"] = appbox::PointerToString(ApcRoutine);
    param["ApcContext"] = appbox::PointerToString(ApcContext);
    param["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    param["FsControlCode"] = FsControlCode;
    param["InputBuffer"] = appbox::PointerToString(InputBuffer);
    param["InputBufferLength"] = InputBufferLength;
    param["OutputBuffer"] = appbox::PointerToString(OutputBuffer);
    param["OutputBufferLength"] = OutputBufferLength;
    return param;
}

static appbox::LoggerF logger("NtFsControlFile", NtFsControlFileLogParam);

static NTSTATUS Hook_NtFsControlFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
                                     PIO_STATUS_BLOCK IoStatusBlock, ULONG FsControlCode, PVOID InputBuffer,
                                     ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength)
{
    logger.Log(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FsControlCode, InputBuffer, InputBufferLength,
               OutputBuffer, OutputBufferLength);
    return sys_NtFsControlFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FsControlCode, InputBuffer,
                               InputBufferLength, OutputBuffer, OutputBufferLength);
}

static void LoadNtFsControlFile()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtFsControlFile");
    sys_NtFsControlFile = reinterpret_cast<T_NtFsControlFile>(addr);
}

appbox::HookRecord appbox::HookNtFsControlFile = {
    "NtFsControlFile",
    LoadNtFsControlFile,
    (void**)&sys_NtFsControlFile,
    Hook_NtFsControlFile,
};
