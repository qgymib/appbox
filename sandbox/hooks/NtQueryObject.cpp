#include "utils/winapi.hpp"
#include "utils/wstring.hpp"
#include "__init__.hpp"
#include "appbox.hpp"
#include <spdlog/spdlog.h>

static NTSTATUS Hook_NtQueryObject_FixName(UNICODE_STRING* name, ULONG ObjectInformationLength,
                                           PULONG ReturnLength)
{
    std::wstring wName = name->Buffer;
    std::wstring wSandboxPath = appbox::mbstowcs(appbox::G->config.sandboxPath.c_str(), CP_UTF8);

    /* If the path is not in the sandbox, do nothing. */
    if (!appbox::StartWith(wName, wSandboxPath))
    {
        return 0;
    }

    /* Remove the leading sandbox path. */
    wName.erase(0, wSandboxPath.size());
    while (wName.front() == L'\\')
    {
        wName.erase(0, 1);
    }
    if (wName.empty())
    {
        spdlog::error(L"Invalid name: {}", name->Buffer);
        return STATUS_INVALID_PARAMETER;
    }
}

static NTSTATUS Hook_NtQueryObject(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass,
                                   PVOID ObjectInformation, ULONG ObjectInformationLength,
                                   PULONG ReturnLength)
{
    auto sys_NtQueryObject =
        reinterpret_cast<appbox::winapi::NtQueryObject>(appbox::NtQueryObject.orig);
    NTSTATUS result = sys_NtQueryObject(Handle, ObjectInformationClass, ObjectInformation,
                                        ObjectInformationLength, ReturnLength);
    if (!NT_SUCCESS(result))
    {
        return result;
    }

    switch (ObjectInformationClass)
    {
    case ObjectNameInformation:
        return Hook_NtQueryObject_FixName(static_cast<UNICODE_STRING*>(ObjectInformation),
                                          ObjectInformationLength, ReturnLength);

    case ObjectBasicInformation:
    case ObjectTypeInformation:
    case ObjectAllTypesInformation:
    case ObjectDataInformation:
    default:
        break;
    }

    return result;
}

appbox::Detour appbox::NtQueryObject = {
    "NtQueryObject", { L"ntdll.dll" }, Hook_NtQueryObject, nullptr
};
