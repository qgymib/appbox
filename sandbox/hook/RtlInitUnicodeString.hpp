#ifndef APPBOX_SANDBOX_HOOK_RTLINITUNICODESTRING_HPP
#define APPBOX_SANDBOX_HOOK_RTLINITUNICODESTRING_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-rtlinitunicodestring
 */
/* clang-format off */
typedef void (*T_RtlInitUnicodeString)(
    /* [OUT] */         PUNICODE_STRING DestinationString,
    /* [IN,OPTIONAL] */ PCWSTR          SourceString
);
/* clang-format on */

/**
 * @brief RtlInitUnicodeString() direct call.
 */
extern T_RtlInitUnicodeString sys_RtlInitUnicodeString;
}

namespace appbox
{

/**
 * @brief Inject RtlInitUnicodeString() hook.
 */
void AttachRtlInitUnicodeString();
void DetachRtlInitUnicodeString();

} // namespace appbox

#endif
