#include "utils/winapi.hpp"
#include "utils/file.hpp"
#include "__init__.hpp"
#include "appbox.hpp"

struct HookNtCreateFileCtx
{
    appbox::winapi::NtQueryObject sys_NtQueryObject;
};

static HookNtCreateFileCtx* hook_NtCreateFileCtx = nullptr;

static BOOL Hook_NtCreateFile_InitOnce(PINIT_ONCE, PVOID, PVOID*)
{
    hook_NtCreateFileCtx = new HookNtCreateFileCtx;
    hook_NtCreateFileCtx->sys_NtQueryObject = reinterpret_cast<appbox::winapi::NtQueryObject>(
        GetProcAddress(appbox::G->hNtdll, "NtQueryObject"));

    return TRUE;
}

static NTSTATUS Hook_NtCreateFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
                                  POBJECT_ATTRIBUTES ObjectAttributes,
                                  PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize,
                                  ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition,
                                  ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    static INIT_ONCE once_token = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once_token, Hook_NtCreateFile_InitOnce, nullptr, nullptr);

    const auto fn = static_cast<appbox::winapi::NtCreateFile>(appbox::NtCreateFile.orig);

    std::wstring wPath;
    if (ObjectAttributes->RootDirectory != nullptr)
    {
        std::array<BYTE, 4096> buffer;
        ULONG                  returnLength;
        NTSTATUS               status = hook_NtCreateFileCtx->sys_NtQueryObject(
            ObjectAttributes->RootDirectory, ObjectNameInformation, buffer.data(),
            (ULONG)buffer.size(), &returnLength);
        if (!NT_SUCCESS(status))
        {
            goto FINISH;
        }

        UNICODE_STRING* objectName = reinterpret_cast<UNICODE_STRING*>(buffer.data());
        wPath = objectName->Buffer;
        appbox::PathConcat(wPath, ObjectAttributes->ObjectName->Buffer);
    }
    else
    {
        wPath = ObjectAttributes->ObjectName->Buffer;
    }

FINISH:
    return fn(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
              FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
}

appbox::Detour appbox::NtCreateFile = {
    "NtCreateFile",
    { L"ntdll.dll" },
    Hook_NtCreateFile,
    nullptr,
};
