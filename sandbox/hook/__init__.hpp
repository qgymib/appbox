#ifndef APPBOX_SANDBOX_HOOK_INIT_HPP
#define APPBOX_SANDBOX_HOOK_INIT_HPP

#include "utils/WinAPI.h"

namespace appbox
{

struct ThreadLocal
{
    static ThreadLocal& Get()
    {
        static thread_local ThreadLocal s_local;
        return s_local;
    }

    bool disable_NtCreateFile_hook = false;
    bool disable_NtQueryObject_hook = false;
};

/**
 * @brief System informations.
 */
struct Sys
{
    /**
     * @brief System build number
     *
     * 7600: Windows 7
     * 8400: Windows 8.1
     * 14942: Windows 10
     */
    ULONG OSBuild;

    /**
     * @brief System DLL handles.
     * @{
     */
    HMODULE h_ntdll;
    HMODULE h_kernel32;
    HMODULE h_kernelbase;
    /**
     * @}
     */
};
extern Sys sys;

/**
 * @brief Initialize hooks
 */
NTSTATUS InitHook();
void ExitHook();

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_INIT_HPP
