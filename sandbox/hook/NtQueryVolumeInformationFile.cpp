#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "hook/NtQueryVolumeInformationFile.hpp"
#include "__init__.hpp"
#include <detours.h>

T_NtQueryVolumeInformationFile sys_NtQueryVolumeInformationFile = nullptr;

static nlohmann::json NtQueryVolumeInformationFileLogParam(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
                                                           PVOID FsInformation, ULONG Length,
                                                           FS_INFORMATION_CLASS FsInformationClass)
{
    nlohmann::json param;
    param["FileHandle"] = appbox::PointerToString(FileHandle);
    param["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    param["FsInformation"] = appbox::PointerToString(FsInformation);
    param["Length"] = Length;
    param["FsInformationClass"] = FsInformationClass;
    return param;
}

static appbox::LoggerF logger("NtQueryVolumeInformationFile", NtQueryVolumeInformationFileLogParam);

static NTSTATUS Hook_NtQueryVolumeInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
                                                  PVOID FsInformation, ULONG Length,
                                                  FS_INFORMATION_CLASS FsInformationClass)
{
    logger.Log(FileHandle, IoStatusBlock, FsInformation, Length, FsInformationClass);
    return sys_NtQueryVolumeInformationFile(FileHandle, IoStatusBlock, FsInformation, Length, FsInformationClass);
}

void appbox::AttachNtQueryVolumeInformationFile()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtQueryVolumeInformationFile");
    sys_NtQueryVolumeInformationFile = reinterpret_cast<T_NtQueryVolumeInformationFile>(addr);

    DetourAttach(&sys_NtQueryVolumeInformationFile, Hook_NtQueryVolumeInformationFile);
}
