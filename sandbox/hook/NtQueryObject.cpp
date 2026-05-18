#include "utils/WinAPI.h" /* Must be first include file */
#include <detours.h>
#include "utils/Log.hpp"
#include "NtQueryObject.hpp"
#include "__init__.hpp"

T_NtQueryObject sys_NtQueryObject = nullptr;

static NTSTATUS Hook_NtQueryObject(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass,
                                   PVOID ObjectInformation, ULONG ObjectInformationLength, PULONG ReturnLength)
{
    return sys_NtQueryObject(Handle, ObjectInformationClass, ObjectInformation, ObjectInformationLength, ReturnLength);
}

void appbox::InjectNtQueryObject()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtQueryObject");
    sys_NtQueryObject = reinterpret_cast<T_NtQueryObject>(addr);

    auto ret = DetourAttach(&sys_NtQueryObject, Hook_NtQueryObject);
    if (ret != NO_ERROR)
    {
        THROW_LOG("DetourAttach(NtQueryObject) failed");
    }
}
