#ifndef APPBOX_SANDBOX_HOOK_NTCREATEUSERPROCESS_HPP
#define APPBOX_SANDBOX_HOOK_NTCREATEUSERPROCESS_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @brief The system call every process creation of the process ends in.
 *
 * The entry point is the lowest user mode point of a creation: the Win32 code
 * of `kernelbase` builds the parameters of the process and calls it, and so does
 * the runtime of the system (`RtlCreateUserProcess`). A sandbox which hooks it
 * therefore sees a creation which never passes the Win32 entry point as well.
 *
 * The image of the process is named by the attribute list and not by the
 * parameters: the entry of the image name carries the buffer of the path and
 * its length in bytes, while the parameters carry the paths the process itself
 * reports.
 *
 * @see https://ntdoc.m417z.com/ntcreateuserprocess
 */
/* clang-format off */
typedef NTSTATUS(NTAPI* T_NtCreateUserProcess)(
    /* [OUT] */              PHANDLE                         ProcessHandle,
    /* [OUT] */              PHANDLE                         ThreadHandle,
    /* [IN] */               ACCESS_MASK                     ProcessDesiredAccess,
    /* [IN] */               ACCESS_MASK                     ThreadDesiredAccess,
    /* [IN,OPTIONAL] */      POBJECT_ATTRIBUTES              ProcessObjectAttributes,
    /* [IN,OPTIONAL] */      POBJECT_ATTRIBUTES              ThreadObjectAttributes,
    /* [IN] */               ULONG                           ProcessFlags,
    /* [IN] */               ULONG                           ThreadFlags,
    /* [IN,OPTIONAL] */      PRTL_USER_PROCESS_PARAMETERS    ProcessParameters,
    /* [IN,OUT] */           PPS_CREATE_INFO                 CreateInfo,
    /* [IN,OUT,OPTIONAL] */  PPS_ATTRIBUTE_LIST              AttributeList
);
/* clang-format on */

/**
 * @brief NtCreateUserProcess() direct call.
 */
extern T_NtCreateUserProcess sys_NtCreateUserProcess;
}

namespace appbox
{

/**
 * @brief Hook NtCreateUserProcess().
 */
extern HookRecord HookNtCreateUserProcess;

} // namespace appbox

#endif
