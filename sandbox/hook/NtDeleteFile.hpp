#ifndef APPBOX_SANDBOX_HOOK_NTDELETEFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTDELETEFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwdeletefile
 */
typedef NTSTATUS (*T_NtDeleteFile)(POBJECT_ATTRIBUTES ObjectAttributes);

/**
 * @brief NtDeleteFile() direct call.
 */
extern T_NtDeleteFile sys_NtDeleteFile;
}

namespace appbox
{

/**
 * @brief Inject NtDeleteFile() hook.
 */
void InjectNtDeleteFile();

} // namespace appbox

#endif
