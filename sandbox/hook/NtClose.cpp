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
        /*
         * A handle which was opened with `FILE_DELETE_ON_CLOSE` removes its
         * object while it is closed, and the object never reports a pending
         * delete for it, so the flag of the record decides that case. Every
         * other handle is asked for the state of the object: only a handle
         * which carries the access to delete it can have a pending delete.
         *
         * A record the directory enumeration adopted carries no knowledge of
         * the call which opened the handle, so the close does not record a
         * delete for it: whether the handle may remove its object is a property
         * of that call, which the record cannot report.
         */
        bPendingDelete = !info->bAdopted && (info->bDeleteOnClose || IsPendingDelete(Handle));
        LOG_T(L"path:{}, bPendingDelete:{}", info->viewPath, bPendingDelete);
    }

    auto st = sys_NtClose(Handle);
    if (NT_SUCCESS(st) && bPendingDelete)
    {
        appbox::DeleteViewPath(*info->resolve, info->viewPath, info->ObjAttributes);
    }
    return st;
}

static void LoadNtClose()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtClose");
    sys_NtClose = reinterpret_cast<T_NtClose>(addr);
}

appbox::HookRecord appbox::HookNtClose = {
    "NtClose",
    LoadNtClose,
    (void**)&sys_NtClose,
    Hook_NtClose,
};
