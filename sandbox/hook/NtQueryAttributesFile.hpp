#ifndef APPBOX_SANDBOX_HOOK_NTQUERYATTRIBUTESFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYATTRIBUTESFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/devnotes/ntqueryattributesfile
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryAttributesFile)(
    /* [IN] */  POBJECT_ATTRIBUTES      ObjectAttributes,
    /* [OUT] */ PFILE_BASIC_INFORMATION FileInformation
);
/* clang-format on */

/**
 * @brief NtQueryAttributesFile() direct call.
 */
extern T_NtQueryAttributesFile sys_NtQueryAttributesFile;
}

namespace appbox
{

/**
 * @brief Inject NtQueryAttributesFile() hook.
 */
void AttachNtQueryAttributesFile();
void DetachNtQueryAttributesFile();

} // namespace appbox

#endif
