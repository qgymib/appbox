#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/GetPEB.hpp"
#include "hook/CreateProcessInternalW.hpp"
#include "hook/LdrQueryImageFileExecutionOptionsEx.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtCompactKeys.hpp"
#include "hook/NtCompressKey.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/NtCreateKey.hpp"
#include "hook/NtCreateKeyTransacted.hpp"
#include "hook/NtCurrentTeb.hpp"
#include "hook/NtDeleteFile.hpp"
#include "hook/NtDeleteKey.hpp"
#include "hook/NtDeleteValueKey.hpp"
#include "hook/NtDeviceIoControlFile.hpp"
#include "hook/NtEnumerateKey.hpp"
#include "hook/NtEnumerateValueKey.hpp"
#include "hook/NtFlushKey.hpp"
#include "hook/NtFsControlFile.hpp"
#include "hook/NtLoadKey.hpp"
#include "hook/NtLoadKey2.hpp"
#include "hook/NtLoadKey3.hpp"
#include "hook/NtLoadKeyEx.hpp"
#include "hook/NtLockRegistryKey.hpp"
#include "hook/NtNotifyChangeKey.hpp"
#include "hook/NtNotifyChangeMultipleKeys.hpp"
#include "hook/NtOpenFile.hpp"
#include "hook/NtOpenKey.hpp"
#include "hook/NtOpenKeyEx.hpp"
#include "hook/NtOpenKeyTransacted.hpp"
#include "hook/NtOpenKeyTransactedEx.hpp"
#include "hook/NtOpenSymbolicLinkObject.hpp"
#include "hook/NtQueryAttributesFile.hpp"
#include "hook/NtQueryDirectoryFile.hpp"
#include "hook/NtQueryDirectoryFileEx.hpp"
#include "hook/NtQueryFullAttributesFile.hpp"
#include "hook/NtQueryInformationByName.hpp"
#include "hook/NtQueryInformationFile.hpp"
#include "hook/NtQueryKey.hpp"
#include "hook/NtQueryMultipleValueKey.hpp"
#include "hook/NtQueryObject.hpp"
#include "hook/NtQueryOpenSubKeys.hpp"
#include "hook/NtQueryOpenSubKeysEx.hpp"
#include "hook/NtQueryValueKey.hpp"
#include "hook/NtQueryVolumeInformationFile.hpp"
#include "hook/NtReadFile.hpp"
#include "hook/NtRenameKey.hpp"
#include "hook/NtReplaceKey.hpp"
#include "hook/NtRestoreKey.hpp"
#include "hook/NtSaveKey.hpp"
#include "hook/NtSaveKeyEx.hpp"
#include "hook/NtSetInformationFile.hpp"
#include "hook/NtSetInformationKey.hpp"
#include "hook/NtSetValueKey.hpp"
#include "hook/NtUnloadKey.hpp"
#include "hook/NtUnloadKey2.hpp"
#include "hook/NtUnloadKeyEx.hpp"
#include "hook/NtWriteFile.hpp"
#include "hook/RtlCompareUnicodeString.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "hook/SetProcessMitigationPolicy.hpp"
#include "__init__.hpp"
#include "Sandbox.hpp"
#include <exception>
#include <detours.h>

/**
 * @brief Hook functions, listed in ASCII order.
 */
static const appbox::HookRecord* s_hooks[] = {
    &appbox::HookCreateProcessInternalW,
    &appbox::HookLdrQueryImageFileExecutionOptionsEx,
    &appbox::HookNtClose,
    &appbox::HookNtCompactKeys,
    &appbox::HookNtCompressKey,
    &appbox::HookNtCreateFile,
    &appbox::HookNtCreateKey,
    &appbox::HookNtCreateKeyTransacted,
    &appbox::HookNtCurrentTeb,
    &appbox::HookNtDeleteFile,
    &appbox::HookNtDeleteKey,
    &appbox::HookNtDeleteValueKey,
    &appbox::HookNtDeviceIoControlFile,
    &appbox::HookNtEnumerateKey,
    &appbox::HookNtEnumerateValueKey,
    &appbox::HookNtFlushKey,
    &appbox::HookNtFsControlFile,
    &appbox::HookNtLoadKey,
    &appbox::HookNtLoadKey2,
    &appbox::HookNtLoadKey3,
    &appbox::HookNtLoadKeyEx,
    &appbox::HookNtLockRegistryKey,
    &appbox::HookNtNotifyChangeKey,
    &appbox::HookNtNotifyChangeMultipleKeys,
    &appbox::HookNtOpenFile,
    &appbox::HookNtOpenKey,
    &appbox::HookNtOpenKeyEx,
    &appbox::HookNtOpenKeyTransacted,
    &appbox::HookNtOpenKeyTransactedEx,
    &appbox::HookNtOpenSymbolicLinkObject,
    &appbox::HookNtQueryAttributesFile,
    &appbox::HookNtQueryDirectoryFile,
    &appbox::HookNtQueryDirectoryFileEx,
    &appbox::HookNtQueryFullAttributesFile,
    &appbox::HookNtQueryInformationByName,
    &appbox::HookNtQueryInformationFile,
    &appbox::HookNtQueryKey,
    &appbox::HookNtQueryMultipleValueKey,
    &appbox::HookNtQueryObject,
    &appbox::HookNtQueryOpenSubKeys,
    &appbox::HookNtQueryOpenSubKeysEx,
    &appbox::HookNtQueryValueKey,
    &appbox::HookNtQueryVolumeInformationFile,
    &appbox::HookNtReadFile,
    &appbox::HookNtRenameKey,
    &appbox::HookNtReplaceKey,
    &appbox::HookNtRestoreKey,
    &appbox::HookNtSaveKey,
    &appbox::HookNtSaveKeyEx,
    &appbox::HookNtSetInformationFile,
    &appbox::HookNtSetInformationKey,
    &appbox::HookNtSetValueKey,
    &appbox::HookNtUnloadKey,
    &appbox::HookNtUnloadKey2,
    &appbox::HookNtUnloadKeyEx,
    &appbox::HookNtWriteFile,
    &appbox::HookRtlCompareUnicodeString,
    &appbox::HookRtlInitUnicodeString,
    &appbox::HookSetProcessMitigationPolicy,
};

appbox::Sys appbox::sys;

NTSTATUS appbox::InitHook()
{
    sys.OSBuild = appbox::GetPEB().ImageBuild;

    sys.h_ntdll = GetModuleHandleW(L"ntdll.dll");
    sys.h_kernel32 = GetModuleHandleW(L"kernel32.dll");
    sys.h_kernelbase = GetModuleHandleW(L"kernelbase.dll");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    for (const auto& hook : s_hooks)
    {
        hook->load_proc_addr_fn();

        if (hook->pDetour != nullptr)
        {
            DetourAttach(hook->ppPointer, hook->pDetour);
        }
    }

    DetourTransactionCommit();

    for (const auto& hook : s_hooks)
    {
        LOG_D("{}: {}", hook->name, *hook->ppPointer);
    }

    return 0;
}

void appbox::ExitHook()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    for (const auto& hook : s_hooks)
    {
        if (hook->pDetour != nullptr)
        {
            DetourDetach(hook->ppPointer, hook->pDetour);
        }
    }

    DetourTransactionCommit();
}
