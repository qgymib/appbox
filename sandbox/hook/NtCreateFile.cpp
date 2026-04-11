#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <detours.h>
#include "__init__.hpp"

static NTSTATUS Hook_NtCreateFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
                                  POBJECT_ATTRIBUTES ObjectAttributes,
                                  PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize,
                                  ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition,
                                  ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    // TODO
    return appbox::sys.NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock,
                                    AllocationSize, FileAttributes, ShareAccess, CreateDisposition,
                                    CreateOptions, EaBuffer, EaLength);
}

APPBOX_SANDBOX_INJECT(NtCreateFile)
{
    APPBOX_GET_PROC(sys.h_ntdll, NtCreateFile);
    DetourAttach(&appbox::sys.NtCreateFile, Hook_NtCreateFile);
}
