#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/GetPEB.hpp"
#include "hook/CreateProcessInternalW.hpp"
#include "hook/LdrQueryImageFileExecutionOptionsEx.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/NtCurrentTeb.hpp"
#include "hook/NtDeleteFile.hpp"
#include "hook/NtDeviceIoControlFile.hpp"
#include "hook/NtFsControlFile.hpp"
#include "hook/NtOpenFile.hpp"
#include "hook/NtQueryAttributesFile.hpp"
#include "hook/NtQueryDirectoryFile.hpp"
#include "hook/NtQueryDirectoryFileEx.hpp"
#include "hook/NtQueryFullAttributesFile.hpp"
#include "hook/NtQueryInformationByName.hpp"
#include "hook/NtQueryInformationFile.hpp"
#include "hook/NtQueryObject.hpp"
#include "hook/NtQueryVolumeInformationFile.hpp"
#include "hook/NtReadFile.hpp"
#include "hook/NtSetInformationFile.hpp"
#include "hook/NtWriteFile.hpp"
#include "hook/RtlCompareUnicodeString.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "hook/SetProcessMitigationPolicy.hpp"
#include "__init__.hpp"
#include "Sandbox.hpp"
#include <exception>
#include <detours.h>

static const appbox::HookRecord* s_hooks[] = {
    &appbox::HookCreateProcessInternalW,
    &appbox::HookLdrQueryImageFileExecutionOptionsEx,
    &appbox::HookNtClose,
    &appbox::HookNtCreateFile,
    &appbox::HookNtCurrentTeb,
    &appbox::HookNtDeleteFile,
    &appbox::HookNtDeviceIoControlFile,
    &appbox::HookNtFsControlFile,
    &appbox::HookNtOpenFile,
    &appbox::HookNtQueryAttributesFile,
    &appbox::HookNtQueryDirectoryFile,
    &appbox::HookNtQueryDirectoryFileEx,
    &appbox::HookNtQueryFullAttributesFile,
    &appbox::HookNtQueryInformationByName,
    &appbox::HookNtQueryInformationFile,
    &appbox::HookNtQueryObject,
    &appbox::HookNtQueryVolumeInformationFile,
    &appbox::HookNtReadFile,
    &appbox::HookNtSetInformationFile,
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

        if (appbox::sandbox->bIsolationMode && hook->pDetour != nullptr)
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
        if (appbox::sandbox->bIsolationMode && hook->pDetour != nullptr)
        {
            DetourDetach(hook->ppPointer, hook->pDetour);
        }
    }

    DetourTransactionCommit();
}
