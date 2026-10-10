#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/Defines.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "environment/Isolation.hpp"
#include "filesystem/Resolve.hpp"
#include "Sandbox.hpp"
#include "CreateProcessInternalW.hpp"
#include "WString.hpp"
#include <detours.h>

T_CreateProcessInternalW sys_CreateProcessInternalW = nullptr;

static nlohmann::json CreateProcessInternalWLogParam(HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                                     LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                                     LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
                                                     ULONG dwCreationFlags, LPVOID lpEnvironment,
                                                     LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo,
                                                     LPPROCESS_INFORMATION lpProcessInformation, PHANDLE hNewToken)
{
    nlohmann::json param;
    param["hToken"] = appbox::PointerToString(hToken);
    if (lpApplicationName != nullptr)
    {
        param["lpApplicationName"] = appbox::WideToUTF8(lpApplicationName);
    }
    if (lpCommandLine != nullptr)
    {
        param["lpCommandLine"] = appbox::WideToUTF8(lpCommandLine);
    }
    param["lpProcessAttributes"] = appbox::PointerToString(lpProcessAttributes);
    param["lpThreadAttributes"] = appbox::PointerToString(lpThreadAttributes);
    param["bInheritHandles"] = bInheritHandles;
    param["dwCreationFlags"] = dwCreationFlags;
    param["lpEnvironment"] = appbox::PointerToString(lpEnvironment);
    if (lpCurrentDirectory != nullptr)
    {
        param["lpCurrentDirectory"] = appbox::WideToUTF8(lpCurrentDirectory);
    }
    param["lpStartupInfo"] = appbox::PointerToString(lpStartupInfo);
    param["lpProcessInformation"] = appbox::PointerToString(lpProcessInformation);
    param["hNewToken"] = appbox::PointerToString(hNewToken);
    return param;
}
static appbox::LoggerF logger("CreateProcessInternalW", CreateProcessInternalWLogParam);

/**
 * @brief Resolve the application path of a process creation into the layer
 *        path which hosts the image.
 *
 * CreateProcessInternalW hands the image path to NtCreateUserProcess, which
 * is not hooked and resolves the path in the host filesystem. A view path
 * which only exists in a lower layer must therefore be rewritten into its
 * host location before the process creation is forwarded.
 *
 * @param[in] lpApplicationName Application path as passed by the caller.
 * @param[out] layer_path Host path (Win32 form) which holds the image.
 * @param[out] reparse_failure Status of a reparse point of the path which the
 *                             view could not resolve, `STATUS_SUCCESS`
 *                             otherwise.
 * @return true when the path was resolved and should be rewritten.
 */
static bool ResolveApplicationPath(LPCWSTR lpApplicationName, std::wstring& layer_path, NTSTATUS& reparse_failure)
{
    reparse_failure = STATUS_SUCCESS;

    if (lpApplicationName == nullptr)
    {
        return false;
    }

    /* Win32 path to DOS NT path. */
    std::wstring nt_path = L"\\??\\";
    nt_path += lpApplicationName;

    std::wstring dos_nt_path;
    if (!appbox::MappingAsDosNtPath(nt_path, dos_nt_path))
    {
        return false;
    }

    auto result = appbox::filesystem::Resolve(dos_nt_path);

    /*
     * A reparse point of the image path which the view could not resolve fails
     * the creation: the forwarded call resolves the path in the host
     * filesystem and would load an image the view never decided about.
     */
    if (!NT_SUCCESS(result->reparseStatus))
    {
        LOG_W(L"failed to resolve the reparse point of the image {}: {}", dos_nt_path, result->reparseStatus);
        reparse_failure = result->reparseStatus;
        return false;
    }

    if (result->status != appbox::filesystem::ResolveResult::Status::Exists)
    {
        /* The image does not exist in the view either, let the original
         * call produce the failure. */
        return false;
    }

    layer_path = result->bInUpper ? result->uPath : result->hPath[0].fPath;

    /* Strip the NT prefix, the parameter expects a Win32 path. */
    static const wchar_t kNtPrefix[] = L"\\??\\";
    if (layer_path.compare(0, 4, kNtPrefix) == 0)
    {
        layer_path.erase(0, 4);
    }

    return true;
}

static BOOL WrapDetourCreateProcessWithDllExW(HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                              LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                              LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
                                              DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
                                              LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation,
                                              PHANDLE hNewToken, LPCSTR lpDllName)
{
    PROCESS_INFORMATION backup;
    if (lpProcessInformation == NULL)
    {
        lpProcessInformation = &backup;
        ZeroMemory(&backup, sizeof(backup));
    }

    if (!sys_CreateProcessInternalW(hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
                                    bInheritHandles, dwCreationFlags | CREATE_SUSPENDED, lpEnvironment,
                                    lpCurrentDirectory, lpStartupInfo, lpProcessInformation, hNewToken))
    {
        return FALSE;
    }

    LPCSTR sz = lpDllName;
    if (!DetourUpdateProcessWithDll(lpProcessInformation->hProcess, &sz, 1) &&
        !DetourProcessViaHelperW(lpProcessInformation->dwProcessId, lpDllName, CreateProcessW))
    {

        TerminateProcess(lpProcessInformation->hProcess, ~0u);
        CloseHandle(lpProcessInformation->hProcess);
        CloseHandle(lpProcessInformation->hThread);
        return FALSE;
    }

    if (!(dwCreationFlags & CREATE_SUSPENDED))
    {
        ResumeThread(lpProcessInformation->hThread);
    }

    if (lpProcessInformation == &backup)
    {
        CloseHandle(lpProcessInformation->hProcess);
        CloseHandle(lpProcessInformation->hThread);
    }
    return TRUE;
}

static BOOL Hook_CreateProcessInternalW(HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                        LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                        LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
                                        ULONG dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
                                        LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation,
                                        PHANDLE hNewToken)
{
    logger.Log(hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles,
               dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation, hNewToken);

    /*
     * Rewrite the application path into the host layer which holds the
     * image. The string is kept alive for the duration of the forwarded
     * call.
     */
    std::wstring layer_path;
    LPCWSTR      effective_app_name = lpApplicationName;
    NTSTATUS     reparse_failure = STATUS_SUCCESS;
    if (ResolveApplicationPath(lpApplicationName, layer_path, reparse_failure))
    {
        effective_app_name = layer_path.c_str();
    }
    else if (!NT_SUCCESS(reparse_failure))
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }

#if defined(_WIN64)
    LPCSTR lpDllName = appbox::sandbox->sandbox64_dos_path.c_str();
#else
    LPCSTR lpDllName = appbox::sandbox->sandbox32_dos_path.c_str();
#endif

    /*
     * The environment of the child is the view of the sandbox. A caller which
     * inherits the environment of this process receives the block of the
     * sandbox, and every child is told that its environment is composed
     * already, so the configuration is not applied to it a second time: the
     * values the merge modes join would be joined twice.
     */
    wchar_t*           environment_block = nullptr;
    LPVOID             effective_environment = lpEnvironment;
    std::string        child_inject_data;
    const std::string* inject_data = &appbox::sandbox->inject_data;

    if (appbox::environment::Isolation::IsEnabled())
    {
        if (lpEnvironment == nullptr)
        {
            environment_block = appbox::environment::Isolation::CreateBlock();
            if (environment_block != nullptr)
            {
                effective_environment = environment_block;
            }
        }

        child_inject_data = appbox::environment::BuildChildInjectData();
        inject_data = &child_inject_data;
    }

    DWORD creation_flags = dwCreationFlags | CREATE_SUSPENDED;
    if (environment_block != nullptr)
    {
        /*
         * The block of the sandbox is Unicode text, and a caller which brings
         * an environment block of its own has to say so as well: without the
         * flag the block is read as ANSI text and the creation fails with
         * `ERROR_INVALID_PARAMETER`.
         */
        creation_flags |= CREATE_UNICODE_ENVIRONMENT;
    }

    const BOOL started = WrapDetourCreateProcessWithDllExW(hToken, effective_app_name, lpCommandLine,
                                                           lpProcessAttributes, lpThreadAttributes, bInheritHandles,
                                                           creation_flags, effective_environment, lpCurrentDirectory,
                                                           lpStartupInfo, lpProcessInformation, hNewToken, lpDllName);

    if (environment_block != nullptr)
    {
        /* The block is read while the process is created and not afterwards. */
        appbox::environment::Isolation::ReleaseBlock(environment_block);
    }

    if (!started)
    {
        return FALSE;
    }

    const GUID guid = APPBOX_SANDBOX_GUID;
    auto       inject_data_sz = static_cast<DWORD>(inject_data->size());
    if (!DetourCopyPayloadToProcess(lpProcessInformation->hProcess, guid, inject_data->c_str(), inject_data_sz))
    {
        auto errcode = GetLastError();
        TerminateProcess(lpProcessInformation->hProcess, errcode);
        CloseHandle(lpProcessInformation->hProcess);
        CloseHandle(lpProcessInformation->hThread);
        return FALSE;
    }

    ResumeThread(lpProcessInformation->hThread);
    return TRUE;
}

static void LoadCreateProcessInternalW()
{
    auto addr = GetProcAddress(appbox::sys.h_kernelbase, "CreateProcessInternalW");
    if (addr == nullptr)
    {
        addr = GetProcAddress(appbox::sys.h_kernel32, "CreateProcessInternalW");
    }

    sys_CreateProcessInternalW = reinterpret_cast<T_CreateProcessInternalW>(addr);
}

appbox::HookRecord appbox::HookCreateProcessInternalW = {
    "CreateProcessInternalW",
    LoadCreateProcessInternalW,
    (void**)&sys_CreateProcessInternalW,
    Hook_CreateProcessInternalW,
};
