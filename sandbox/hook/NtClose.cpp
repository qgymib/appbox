#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/HandleInfo.hpp"
#include "hook/NtDeleteFile.hpp"
#include "hook/NtQueryInformationFile.hpp"
#include "NtClose.hpp"

T_NtClose sys_NtClose = nullptr;

static nlohmann::json NtCloseLogParam(HANDLE Handle)
{
    nlohmann::json param;
    param["Handle"] = appbox::PointerToString(Handle);
    return param;
}
static appbox::LoggerF logger("NtClose", NtCloseLogParam);

static bool IsPendingDelete(HANDLE hFile)
{
    IO_STATUS_BLOCK           iosb;
    FILE_STANDARD_INFORMATION fsi;

    NTSTATUS st = sys_NtQueryInformationFile(hFile, &iosb, &fsi, sizeof(fsi), FileStandardInformation);
    if (!NT_SUCCESS(st))
    {
        return false;
    }
    return fsi.DeletePending;
}

static NTSTATUS Hook_NtClose(HANDLE Handle)
{
    logger.Log(Handle);

    auto info = appbox::HandleInfo::Pop(Handle);
    bool bPendingDelete = false;
    if (info.get() != nullptr)
    {
        bPendingDelete = IsPendingDelete(Handle);
        LOG_T(L"path:{}, bPendingDelete:{}", info->viewPath, bPendingDelete);
    }

    auto st = sys_NtClose(Handle);
    if (NT_SUCCESS(st) && bPendingDelete)
    {
        appbox::DeleteViewPath(*info->resolve, info->ObjAttributes);
    }
    return st;
}

static void LoadNtClose()
{
    *appbox::HookNtClose.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtClose.name);
}

appbox::HookRecord appbox::HookNtClose = {
    "NtClose",
    LoadNtClose,
    (void**)&sys_NtClose,
    Hook_NtClose,
};
