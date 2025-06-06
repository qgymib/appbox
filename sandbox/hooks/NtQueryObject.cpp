#include "utils/winapi.hpp"
#include "__init__.hpp"

static NTSTATUS Hook_NtQueryObject(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass,
                                   PVOID ObjectInformation, ULONG ObjectInformationLength,
                                   PULONG ReturnLength)
{
    auto sys_NtQueryObject =
        reinterpret_cast<appbox::winapi::NtQueryObject>(appbox::NtQueryObject.orig);
    return sys_NtQueryObject(Handle, ObjectInformationClass, ObjectInformation,
                             ObjectInformationLength, ReturnLength);
}

appbox::Detour appbox::NtQueryObject = {
    "NtQueryObject", { L"ntdll.dll" }, Hook_NtQueryObject, nullptr
};
