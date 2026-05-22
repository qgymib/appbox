#ifndef APPBOX_SANDBOX_HOOK_NTQUERYATTRIBUTESFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYATTRIBUTESFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/devnotes/ntqueryattributesfile
 */
typedef NTSTATUS (*T_NtQueryAttributesFile)(POBJECT_ATTRIBUTES      ObjectAttributes,
                                            PFILE_BASIC_INFORMATION FileInformation);

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
void InjectNtQueryAttributesFile();

} // namespace appbox

#endif
