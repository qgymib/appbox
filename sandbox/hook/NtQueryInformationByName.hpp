#ifndef APPBOX_SANDBOX_HOOK_NTQUERYINFORMATIONBYNAME_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYINFORMATIONBYNAME_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationbyname
 */
typedef NTSTATUS (*T_NtQueryInformationByName)(POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock,
                                               PVOID FileInformation, ULONG Length,
                                               FILE_INFORMATION_CLASS FileInformationClass);

/**
 * @brief NtQueryInformationByName() direct call.
 */
extern T_NtQueryInformationByName sys_NtQueryInformationByName;
}

namespace appbox
{

/**
 * @brief Inject NtQueryInformationByName() hook.
 */
void InjectNtQueryInformationByName();

} // namespace appbox

#endif
