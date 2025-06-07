#include <ntstatus.h>
#define WIN32_NO_STATUS
#include "utils/winapi.hpp"
#include "utils/wstring.hpp"
#include "utils/FolderConv.hpp"
#include "__init__.hpp"
#include "appbox.hpp"
#include <spdlog/spdlog.h>

struct HookNtQueryObjectCtx
{
    appbox::FolderConv folderConv;
};

static HookNtQueryObjectCtx* hookNtQueryObjectCtx = nullptr;

static NTSTATUS Hook_NtQueryObject_FixName(UNICODE_STRING* name, ULONG ObjectInformationLength,
                                           PULONG ReturnLength)
{
    std::wstring wPath = name->Buffer;
    std::wstring wSandboxPath = appbox::mbstowcs(appbox::G->config.sandboxPath.c_str(), CP_UTF8);

    /* If the path is not in the sandbox, do nothing. */
    if (!appbox::StartWith(wPath, wSandboxPath))
    {
        return STATUS_SUCCESS;
    }

    /* Remove the leading sandbox path. */
    wPath.erase(0, wSandboxPath.size());
    while (wPath.front() == L'\\')
    {
        wPath.erase(0, 1);
    }
    /* In this case the path point to the sandbox, which should not happen. */
    if (wPath.empty())
    {
        spdlog::error(L"Invalid name: {}", name->Buffer);
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Decode the sandbox path as a fake filesystem path.
     * E.G. `@ProgramFiles@\foo` -> `C:\Program Files\foo`
     */
    wPath = hookNtQueryObjectCtx->folderConv.Decode(wPath);
    if (wPath.size() + sizeof(*name) >= ObjectInformationLength)
    {
        spdlog::error(L"[NtQueryObject/ObjectNameInformation] Buffer too small for path `{}`",
                      wPath.c_str());
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* Fix information. */
    name->MaximumLength = static_cast<USHORT>(ObjectInformationLength - sizeof(*name));
    name->Length = static_cast<USHORT>(wPath.size() * sizeof(WCHAR));
    name->Buffer = reinterpret_cast<PWSTR>(name + 1);
    memcpy(name->Buffer, wPath.c_str(), wPath.size() * sizeof(WCHAR));
    name->Buffer[wPath.size()] = L'\0';
    *ReturnLength = static_cast<ULONG>(name->Length + sizeof(*name));
    return STATUS_SUCCESS;
}

static BOOL Hook_NtQueryObject_Init(PINIT_ONCE, PVOID, PVOID*)
{
    hookNtQueryObjectCtx = new HookNtQueryObjectCtx;
    return TRUE;
}

static NTSTATUS Hook_NtQueryObject(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass,
                                   PVOID ObjectInformation, ULONG ObjectInformationLength,
                                   PULONG ReturnLength)
{
    static INIT_ONCE once_token = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once_token, Hook_NtQueryObject_Init, nullptr, nullptr);

    auto sys_NtQueryObject =
        reinterpret_cast<appbox::winapi::NtQueryObject>(appbox::NtQueryObject.orig);
    NTSTATUS result = sys_NtQueryObject(Handle, ObjectInformationClass, ObjectInformation,
                                        ObjectInformationLength, ReturnLength);
    if (!NT_SUCCESS(result) || ObjectInformationClass != ObjectNameInformation)
    {
        return result;
    }

    return Hook_NtQueryObject_FixName(static_cast<UNICODE_STRING*>(ObjectInformation),
                                      ObjectInformationLength, ReturnLength);
}

appbox::Detour appbox::NtQueryObject = {
    "NtQueryObject", { L"ntdll.dll" }, Hook_NtQueryObject, nullptr
};
