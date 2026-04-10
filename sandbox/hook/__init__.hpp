#ifndef APPBOX_SANDBOX_HOOK_INIT_HPP
#define APPBOX_SANDBOX_HOOK_INIT_HPP

#include "WinAPI.hpp"

/**
 * @brief Registered hook functions
 */
/* clang-format off */
#define APPBOX_SANDBOX_HOOKS(xx)            \
    xx(CreateProcessInternalW)              \
    xx(LdrQueryImageFileExecutionOptionsEx) \
    xx(NtCreateFile)                        \
    xx(SetProcessMitigationPolicy)
/* clang-format on */

/**
 * @brief Register hook function
 *
 * Inject function using #DetourAttach(). If any error happens, it is safe to throw exception.
 *
 * Do not call #DetourTransactionBegin() or #DetourTransactionCommit()
 *
 * @param[in] NAME Function name
 */
#define APPBOX_SANDBOX_INJECT(NAME) void appbox::Register_##NAME()

/**
 * @brief Get system API function and assign to #appbox::sys.
 * @param[in] DLL Handle
 * @param[in] NAME Function name.
 */
#define APPBOX_GET_PROC(DLL, NAME)                                                                 \
    do                                                                                             \
    {                                                                                              \
        auto fn = reinterpret_cast<void*>(GetProcAddress(DLL, #NAME));                             \
        appbox::sys.NAME = reinterpret_cast<decltype(appbox::sys.NAME)>(fn);                       \
    } while (0)

namespace appbox
{

/**
 * Import hook functions
 * @{
 */
#define EXPAND_APPBOX_SANDBOX_HOOKS(NAME) void Register_##NAME();
APPBOX_SANDBOX_HOOKS(EXPAND_APPBOX_SANDBOX_HOOKS)
#undef EXPAND_APPBOX_SANDBOX_HOOKS
/**
 * @}
 */

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

    /**
     * @brief System API functions.
     */
#define EXPAND_SANDBOX_AS_SYSAPI(NAME) ::appbox::winapi::NAME NAME;
    APPBOX_SANDBOX_HOOKS(EXPAND_SANDBOX_AS_SYSAPI)
#undef EXPAND_SANDBOX_AS_SYSAPI
    /**
     * @}
     */
};
extern Sys sys;

/**
 * @brief Initialize hooks
 */
void InitHook();

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_INIT_HPP
