#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "NtDeviceIoControlFile.hpp"

T_NtDeviceIoControlFile sys_NtDeviceIoControlFile = nullptr;

static nlohmann::json NtDeviceIoControlFileLogParam(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                                    PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock,
                                                    ULONG IoControlCode, PVOID InputBuffer, ULONG InputBufferLength,
                                                    PVOID OutputBuffer, ULONG OutputBufferLength)
{
    nlohmann::json param;
    param["FileHandle"] = appbox::PointerToString(FileHandle);
    param["Event"] = appbox::PointerToString(Event);
    param["ApcRoutine"] = appbox::PointerToString(ApcRoutine);
    param["ApcContext"] = appbox::PointerToString(ApcContext);
    param["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    param["IoControlCode"] = IoControlCode;
    param["InputBuffer"] = appbox::PointerToString(InputBuffer);
    param["InputBufferLength"] = InputBufferLength;
    param["OutputBuffer"] = appbox::PointerToString(OutputBuffer);
    param["OutputBufferLength"] = OutputBufferLength;
    return param;
}

static appbox::LoggerF logger("NtDeviceIoControlFile", NtDeviceIoControlFileLogParam);

static NTSTATUS Hook_NtDeviceIoControlFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                           PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, ULONG IoControlCode,
                                           PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer,
                                           ULONG OutputBufferLength)
{
    logger.Log(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, IoControlCode, InputBuffer, InputBufferLength,
               OutputBuffer, OutputBufferLength);
    return sys_NtDeviceIoControlFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, IoControlCode,
                                     InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength);
}

static void LoadNtDeviceIoControlFile()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtDeviceIoControlFile");
    sys_NtDeviceIoControlFile = reinterpret_cast<T_NtDeviceIoControlFile>(addr);
}

appbox::HookRecord appbox::HookNtDeviceIoControlFile = {
    "NtDeviceIoControlFile",
    LoadNtDeviceIoControlFile,
    (void**)&sys_NtDeviceIoControlFile,
    Hook_NtDeviceIoControlFile,
};
