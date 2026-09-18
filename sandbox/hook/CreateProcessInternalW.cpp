#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/Defines.hpp"
#include "utils/MappingAsDosNtPath.hpp"
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
 * @return true when the path was resolved and should be rewritten.
 */
static bool ResolveApplicationPath(LPCWSTR lpApplicationName, std::wstring& layer_path)
{
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
    if (ResolveApplicationPath(lpApplicationName, layer_path))
    {
        effective_app_name = layer_path.c_str();
    }

#if defined(_WIN64)
    LPCSTR lpDllName = appbox::sandbox->sandbox64_dos_path.c_str();
#else
    LPCSTR lpDllName = appbox::sandbox->sandbox32_dos_path.c_str();
#endif

    if (!WrapDetourCreateProcessWithDllExW(hToken, effective_app_name, lpCommandLine, lpProcessAttributes,
                                           lpThreadAttributes, bInheritHandles, dwCreationFlags | CREATE_SUSPENDED,
                                           lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation,
                                           hNewToken, lpDllName))
    {
        return FALSE;
    }

    const GUID guid = APPBOX_SANDBOX_GUID;
    auto       inject_data_sz = static_cast<DWORD>(appbox::sandbox->inject_data.size());
    if (!DetourCopyPayloadToProcess(lpProcessInformation->hProcess, guid, appbox::sandbox->inject_data.c_str(),
                                    inject_data_sz))
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
