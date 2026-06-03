#ifndef APPBOX_SANDBOX_HOOK_LDRQUERYIMAGEFILEEXECUTIONOPTIONSEX_HPP
#define APPBOX_SANDBOX_HOOK_LDRQUERYIMAGEFILEEXECUTIONOPTIONSEX_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see
 * https://www.geoffchappell.com/studies/windows/win32/ntdll/api/rtl/rtlexec/queryimagefileexecutionoptionsex.htm
 */
/* clang-format off */
typedef NTSTATUS (*T_LdrQueryImageFileExecutionOptionsEx)(
    /* [IN] */              PUNICODE_STRING lpImageFile,
    /* [IN] */              PCWSTR          lpszOption,
    /* [IN] */              ULONG           dwType,
    /* [OUT] */             PVOID           lpData,
    /* [IN] */              ULONG           cbData,
    /* [OUT,OPTIONAL] */    ULONG*          lpcbData,
    /* [IN] */              BOOLEAN         bWow64
);
/* clang-format on */

/**
 * @brief LdrQueryImageFileExecutionOptionsEx() direct call.
 */
extern T_LdrQueryImageFileExecutionOptionsEx sys_LdrQueryImageFileExecutionOptionsEx;
}

namespace appbox
{

/**
 * @brief Hook LdrQueryImageFileExecutionOptionsEx().
 */
extern HookRecord HookLdrQueryImageFileExecutionOptionsEx;

} // namespace appbox

#endif
