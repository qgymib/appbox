#ifndef APPBOX_SANDBOX_HOOK_NTQUERYFULLATTRIBUTESFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYFULLATTRIBUTESFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwqueryfullattributesfile
 */
typedef NTSTATUS (*T_NtQueryFullAttributesFile)(POBJECT_ATTRIBUTES             ObjectAttributes,
                                                PFILE_NETWORK_OPEN_INFORMATION FileInformation);

/**
 * @brief NtQueryFullAttributesFile() direct call.
 */
extern T_NtQueryFullAttributesFile sys_NtQueryFullAttributesFile;
}

namespace appbox
{

/**
 * @brief Inject NtQueryFullAttributesFile() hook.
 */
void InjectNtQueryFullAttributesFile();

} // namespace appbox

#endif
