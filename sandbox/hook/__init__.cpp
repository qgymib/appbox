#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/FontApi.hpp"
#include "utils/GetPEB.hpp"
#include "utils/NameResolution.hpp"
#include "utils/ProxyHook.hpp"
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
#include "hook/NtGdiAddFontResourceW.hpp"
#include "hook/NtGdiRemoveFontResourceW.hpp"
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
#include "hook/DnsQuery_A.hpp"
#include "hook/DnsQuery_UTF8.hpp"
#include "hook/DnsQuery_W.hpp"
#include "hook/ExpandEnvironmentStringsA.hpp"
#include "hook/ExpandEnvironmentStringsW.hpp"
#include "hook/FreeEnvironmentStringsA.hpp"
#include "hook/FreeEnvironmentStringsW.hpp"
#include "hook/GetAddrInfoExW.hpp"
#include "hook/GetAddrInfoW.hpp"
#include "hook/getaddrinfo.hpp"
#include "hook/GetEnvironmentStringsA.hpp"
#include "hook/GetEnvironmentStringsW.hpp"
#include "hook/GetEnvironmentVariableA.hpp"
#include "hook/GetEnvironmentVariableW.hpp"
#include "hook/gethostbyname.hpp"
#include "hook/closesocket.hpp"
#include "hook/connect.hpp"
#include "hook/recvfrom.hpp"
#include "hook/RtlCompareUnicodeString.hpp"
#include "hook/RtlCreateEnvironment.hpp"
#include "hook/RtlExpandEnvironmentStrings_U.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "hook/RtlQueryEnvironmentVariable.hpp"
#include "hook/RtlQueryEnvironmentVariable_U.hpp"
#include "hook/RtlSetEnvironmentVariable.hpp"
#include "hook/sendto.hpp"
#include "hook/SetEnvironmentVariableA.hpp"
#include "hook/SetEnvironmentVariableW.hpp"
#include "hook/SetProcessMitigationPolicy.hpp"
#include "hook/WSAConnect.hpp"
#include "hook/WSARecvFrom.hpp"
#include "hook/WSASendTo.hpp"
#include "__init__.hpp"
#include "utils/HookTransaction.hpp"
#include "Sandbox.hpp"
#include <exception>
#include <iterator>
#include <detours.h>

static const appbox::HookRecord* s_hooks[] = {
    &appbox::HookCloseSocket,
    &appbox::HookConnect,
    &appbox::HookCreateProcessInternalW,
    &appbox::HookDnsQueryA,
    &appbox::HookDnsQueryUTF8,
    &appbox::HookDnsQueryW,
    &appbox::HookExpandEnvironmentStringsA,
    &appbox::HookExpandEnvironmentStringsW,
    &appbox::HookFreeEnvironmentStringsA,
    &appbox::HookFreeEnvironmentStringsW,
    &appbox::HookGetAddrInfo,
    &appbox::HookGetAddrInfoExW,
    &appbox::HookGetAddrInfoW,
    &appbox::HookGetEnvironmentStringsA,
    &appbox::HookGetEnvironmentStringsW,
    &appbox::HookGetEnvironmentVariableA,
    &appbox::HookGetEnvironmentVariableW,
    &appbox::HookGetHostByName,
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
    &appbox::HookNtGdiAddFontResourceW,
    &appbox::HookNtGdiRemoveFontResourceW,
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
    &appbox::HookRecvFrom,
    &appbox::HookRtlCompareUnicodeString,
    &appbox::HookRtlCreateEnvironment,
    &appbox::HookRtlExpandEnvironmentStrings_U,
    &appbox::HookRtlInitUnicodeString,
    &appbox::HookRtlQueryEnvironmentVariable,
    &appbox::HookRtlQueryEnvironmentVariable_U,
    &appbox::HookRtlSetEnvironmentVariable,
    &appbox::HookSendTo,
    &appbox::HookSetEnvironmentVariableA,
    &appbox::HookSetEnvironmentVariableW,
    &appbox::HookSetProcessMitigationPolicy,
    &appbox::HookWSAConnect,
    &appbox::HookWSARecvFrom,
    &appbox::HookWSASendTo,
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
     * The name resolution of an application lives in modules which a process
     * loads on demand, so they are loaded before the entry points are resolved:
     * a hook which cannot be resolved is fatal in isolation mode, because the
     * sandbox would silently stop redirecting the name resolution.
     */
    if (!appbox::network::LoadNameResolutionModules())
    {
        LOG_E("failed to load the modules of the name resolution");
        return STATUS_DLL_NOT_FOUND;
    }

    /*
     * The entry point of the font resource call lives in the window manager
     * module, which a process loads on demand as well: it is loaded before the
     * entry points are resolved, because a hook which carries a detour and
     * cannot be resolved is fatal in isolation mode. Outside isolation mode the
     * modules are left alone, so a process which is not sandboxed does not load
     * the graphics modules because of the sandbox.
     */
    if (appbox::sandbox->bIsolationMode && !appbox::fonts::LoadFontModules())
    {
        LOG_E("failed to load the modules of the font resources");
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
        LOG_E("failed to attach hooks ({}): {}", result.pFailedHook != nullptr ? result.pFailedHook : "transaction",
              result.status);
        return STATUS_UNSUCCESSFUL;
    }

    /*
     * The entry points of the proxy are installed after the hooks are
     * attached: the saved pointers carry the trampoline to the original code
     * from that moment on, so the proxy reaches its server without passing the
     * hooks again.
     */
    appbox::network::InstallRawSocketApi();

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
        LOG_E("failed to detach hooks ({}): {}", result.pFailedHook != nullptr ? result.pFailedHook : "transaction",
              result.status);
    }
}
