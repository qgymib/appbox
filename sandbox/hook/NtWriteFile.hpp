#ifndef APPBOX_SANDBOX_HOOK_NTWRITEFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTWRITEFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntwritefile
 */
typedef NTSTATUS (*T_NtWriteFile)(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
                                  PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset,
                                  PULONG Key);

extern T_NtWriteFile sys_NtWriteFile;
}

namespace appbox
{

void InjectNtWriteFile();
}

#endif
