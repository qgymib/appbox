#ifndef APPBOX_SANDBOX_HOOK_NTQUERYFULLATTRIBUTESFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYFULLATTRIBUTESFILE_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwqueryfullattributesfile
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryFullAttributesFile)(
    /* [IN] */  POBJECT_ATTRIBUTES              ObjectAttributes,
    /* [OUT] */ PFILE_NETWORK_OPEN_INFORMATION  FileInformation
);
/* clang-format on */

/**
 * @brief NtQueryFullAttributesFile() direct call.
 */
extern T_NtQueryFullAttributesFile sys_NtQueryFullAttributesFile;
}

namespace appbox
{

/**
 * @brief Hook NtQueryFullAttributesFile().
 */
extern HookRecord HookNtQueryFullAttributesFile;

} // namespace appbox

#endif
