#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <detours.h>
#include "__init__.hpp"

static NTSTATUS Hook_NtQueryObject(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass,
                                   PVOID ObjectInformation, ULONG ObjectInformationLength,
                                   PULONG ReturnLength)
{
    // TODO
    return appbox::sys.NtQueryObject(Handle, ObjectInformationClass, ObjectInformation,
                                     ObjectInformationLength, ReturnLength);
}

APPBOX_SANDBOX_INJECT(NtQueryObject)
{
    APPBOX_GET_PROC(sys.h_ntdll, NtQueryObject);
    DetourAttach(&appbox::sys.NtQueryObject, Hook_NtQueryObject);
}
