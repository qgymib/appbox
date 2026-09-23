#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/GetPEB.hpp"
#include "hook/CreateProcessInternalW.hpp"
#include "hook/LdrQueryImageFileExecutionOptionsEx.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/NtCreateKey.hpp"
#include "hook/NtCurrentTeb.hpp"
#include "hook/NtDeleteFile.hpp"
#include "hook/NtDeleteKey.hpp"
#include "hook/NtDeleteValueKey.hpp"
#include "hook/NtDeviceIoControlFile.hpp"
#include "hook/NtEnumerateKey.hpp"
#include "hook/NtEnumerateValueKey.hpp"
#include "hook/NtFsControlFile.hpp"
#include "hook/NtOpenFile.hpp"
#include "hook/NtOpenKey.hpp"
#include "hook/NtOpenKeyEx.hpp"
#include "hook/NtQueryAttributesFile.hpp"
#include "hook/NtQueryDirectoryFile.hpp"
#include "hook/NtQueryDirectoryFileEx.hpp"
#include "hook/NtQueryFullAttributesFile.hpp"
#include "hook/NtQueryInformationByName.hpp"
#include "hook/NtQueryInformationFile.hpp"
#include "hook/NtQueryKey.hpp"
#include "hook/NtQueryMultipleValueKey.hpp"
#include "hook/NtQueryObject.hpp"
#include "hook/NtQueryValueKey.hpp"
#include "hook/NtQueryVolumeInformationFile.hpp"
#include "hook/NtReadFile.hpp"
#include "hook/NtSaveKey.hpp"
#include "hook/NtSaveKeyEx.hpp"
#include "hook/NtSetInformationFile.hpp"
#include "hook/NtWriteFile.hpp"
#include "hook/RtlCompareUnicodeString.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "hook/SetProcessMitigationPolicy.hpp"
#include "__init__.hpp"
#include "HookTransaction.hpp"
#include "Sandbox.hpp"
#include <exception>
#include <iterator>
#include <detours.h>

static const appbox::HookRecord* s_hooks[] = {
    &appbox::HookCreateProcessInternalW,
    &appbox::HookLdrQueryImageFileExecutionOptionsEx,
    &appbox::HookNtClose,
    &appbox::HookNtCreateFile,
    &appbox::HookNtCreateKey,
    &appbox::HookNtCurrentTeb,
    &appbox::HookNtDeleteFile,
    &appbox::HookNtDeleteKey,
    &appbox::HookNtDeleteValueKey,
    &appbox::HookNtDeviceIoControlFile,
    &appbox::HookNtEnumerateKey,
    &appbox::HookNtEnumerateValueKey,
    &appbox::HookNtFsControlFile,
    &appbox::HookNtOpenFile,
    &appbox::HookNtOpenKey,
    &appbox::HookNtOpenKeyEx,
    &appbox::HookNtQueryAttributesFile,
    &appbox::HookNtQueryDirectoryFile,
    &appbox::HookNtQueryDirectoryFileEx,
    &appbox::HookNtQueryFullAttributesFile,
    &appbox::HookNtQueryInformationByName,
    &appbox::HookNtQueryInformationFile,
    &appbox::HookNtQueryKey,
    &appbox::HookNtQueryMultipleValueKey,
    &appbox::HookNtQueryObject,
    &appbox::HookNtQueryValueKey,
    &appbox::HookNtQueryVolumeInformationFile,
    &appbox::HookNtReadFile,
    &appbox::HookNtSaveKey,
    &appbox::HookNtSaveKeyEx,
    &appbox::HookNtSetInformationFile,
    &appbox::HookNtWriteFile,
    &appbox::HookRtlCompareUnicodeString,
    &appbox::HookRtlInitUnicodeString,
    &appbox::HookSetProcessMitigationPolicy,
};

appbox::Sys appbox::sys;

NTSTATUS appbox::InitHook()
{
    if (appbox::sandbox == nullptr)
    {
        return STATUS_UNSUCCESSFUL;
    }

    sys.OSBuild = appbox::GetPEB().ImageBuild;

    sys.h_ntdll = GetModuleHandleW(L"ntdll.dll");
    sys.h_kernel32 = GetModuleHandleW(L"kernel32.dll");
    sys.h_kernelbase = GetModuleHandleW(L"kernelbase.dll");

    if (sys.h_ntdll == nullptr || sys.h_kernel32 == nullptr || sys.h_kernelbase == nullptr)
    {
        LOG_E("failed to resolve the system module handles");
        return STATUS_DLL_NOT_FOUND;
    }

    /*
     * Resolve the entry point of every hook. In isolation mode a hook which
     * cannot be resolved is fatal when it carries a detour, because the sandbox
     * would silently stop isolating the corresponding API.
     */
    for (const auto& hook : s_hooks)
    {
        hook->load_proc_addr_fn();

        if (*hook->ppPointer != nullptr)
        {
            continue;
        }

        if (hook->pDetour != nullptr && appbox::sandbox->bIsolationMode)
        {
            LOG_E("failed to resolve the entry point of {}", hook->name);
            return STATUS_PROCEDURE_NOT_FOUND;
        }

        LOG_W("the entry point of {} was not resolved", hook->name);
    }

    if (!appbox::sandbox->bIsolationMode)
    {
        /* Outside isolation mode the hooks are resolved but never attached. */
        return STATUS_SUCCESS;
    }

    const appbox::HookTransactionResult result = appbox::ApplyHookTransaction(
        s_hooks, std::size(s_hooks), appbox::HookAction::Attach, appbox::DefaultDetourOps());
    if (!result.bSuccess)
    {
        LOG_E("failed to attach hooks ({}): {}",
              result.pFailedHook != nullptr ? result.pFailedHook : "transaction", result.status);
        return STATUS_UNSUCCESSFUL;
    }

    for (const auto& hook : s_hooks)
    {
        LOG_D("{}: {}", hook->name, *hook->ppPointer);
    }

    return STATUS_SUCCESS;
}

void appbox::ExitHook()
{
    if (appbox::sandbox == nullptr || !appbox::sandbox->bIsolationMode)
    {
        /* Nothing was attached outside isolation mode. */
        return;
    }

    const appbox::HookTransactionResult result = appbox::ApplyHookTransaction(
        s_hooks, std::size(s_hooks), appbox::HookAction::Detach, appbox::DefaultDetourOps());
    if (!result.bSuccess)
    {
        LOG_E("failed to detach hooks ({}): {}",
              result.pFailedHook != nullptr ? result.pFailedHook : "transaction", result.status);
    }
}
