#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <detours.h>
#include "utils/Log.hpp"
#include "__init__.hpp"
#include "NtCreateFile.hpp"

T_NtCreateFile sys_NtCreateFile = nullptr;

static NTSTATUS Hook_NtCreateFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                  PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes,
                                  ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer,
                                  ULONG EaLength)
{
    return sys_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes,
                            ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
}

void appbox::InjectNtCreateFile()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtCreateFile");
    sys_NtCreateFile = reinterpret_cast<T_NtCreateFile>(addr);

    auto ret = DetourAttach(&sys_NtCreateFile, Hook_NtCreateFile);
    if (ret != NO_ERROR)
    {
        THROW_LOG("DetourAttach(NtCreateFile) failed");
    }
}
