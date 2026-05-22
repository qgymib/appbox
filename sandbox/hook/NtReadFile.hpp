#ifndef APPBOX_SANDBOX_HOOK_NTREADFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTREADFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/devnotes/ntreadfile
 */
typedef NTSTATUS (*T_NtReadFile)(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
                                 PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset,
                                 PULONG Key);

extern T_NtReadFile sys_NtReadFile;
}

namespace appbox
{

void InjectNtReadFile();

}

#endif
