#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <detours.h>
#include <string>
#include "WinAPI.hpp"
#include "WString.hpp"
#include "__init__.hpp"
#include "Sandbox.hpp"

static NTSTATUS Hook_NtQueryObject(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass,
                                   PVOID ObjectInformation, ULONG ObjectInformationLength,
                                   PULONG ReturnLength)
{
    NTSTATUS status = appbox::sys.NtQueryObject(Handle, ObjectInformationClass, ObjectInformation,
                                                ObjectInformationLength, ReturnLength);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    /* Only care about ObjectNameInformation */
    if (ObjectInformationClass != ObjectNameInformation)
    {
        return status;
    }

    /* Get returned path */
    auto nameInfo = reinterpret_cast<OBJECT_NAME_INFORMATION*>(ObjectInformation);
    if (nameInfo == nullptr || nameInfo->Name.Buffer == nullptr || nameInfo->Name.Length == 0)
    {
        return status;
    }
    std::wstring returnedPath(nameInfo->Name.Buffer, nameInfo->Name.Length / sizeof(WCHAR));
    if (!appbox::StartsWith(returnedPath, appbox::sandbox->sandbox_path))
    {
        return status;
    }

    // TODO: Replace as native system path.
    return status;
}

APPBOX_SANDBOX_INJECT(NtQueryObject)
{
    APPBOX_GET_PROC(sys.h_ntdll, NtQueryObject);
    DetourAttach(&appbox::sys.NtQueryObject, Hook_NtQueryObject);
}
