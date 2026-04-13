#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <detours.h>
#include <string>
#include "utils/Log.hpp"
#include "__init__.hpp"

static NTSTATUS Hook_NtCreateFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                  PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes,
                                  ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer,
                                  ULONG EaLength)
{
    auto result =
        appbox::sys.NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
                                 FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);

    LOG_T(L"NtCreateFile(FileHandle={}, DesiredAccess={}, ObjectAttributes={ Length={}, RootDirectory={}, "
          "ObjectName={}, Attributes={} }, IoStatusBlock={}, AllocationSize={}, FileAttributes={}, ShareAccess={}, "
          "CreateDisposition={}, CreateOptions={}, EaBuffer={}, EaLength={})={}",
          static_cast<void*>(FileHandle), DesiredAccess, ObjectAttributes->Length, ObjectAttributes->RootDirectory,
          ObjectAttributes->ObjectName == nullptr ? L"nullptr" : ObjectAttributes->ObjectName->Buffer,
          ObjectAttributes->Attributes, static_cast<void*>(IoStatusBlock),
          AllocationSize == nullptr ? L"nullptr" : std::to_wstring(AllocationSize->QuadPart), FileAttributes,
          ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength, result);

    return result;
}

APPBOX_SANDBOX_INJECT(NtCreateFile)
{
    APPBOX_GET_PROC(sys.h_ntdll, NtCreateFile);
    DetourAttach(&appbox::sys.NtCreateFile, Hook_NtCreateFile);
}
