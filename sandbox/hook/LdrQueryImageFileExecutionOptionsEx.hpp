#ifndef APPBOX_SANDBOX_HOOK_LDRQUERYIMAGEFILEEXECUTIONOPTIONSEX_HPP
#define APPBOX_SANDBOX_HOOK_LDRQUERYIMAGEFILEEXECUTIONOPTIONSEX_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see
 * https://www.geoffchappell.com/studies/windows/win32/ntdll/api/rtl/rtlexec/queryimagefileexecutionoptionsex.htm
 */
typedef NTSTATUS (*T_LdrQueryImageFileExecutionOptionsEx)(PUNICODE_STRING lpImageFile, PCWSTR lpszOption, ULONG dwType,
                                                          PVOID lpData, ULONG cbData, ULONG* lpcbData, BOOLEAN bWow64);

extern T_LdrQueryImageFileExecutionOptionsEx sys_LdrQueryImageFileExecutionOptionsEx;
}

namespace appbox
{

void InjectLdrQueryImageFileExecutionOptionsEx();

}

#endif
