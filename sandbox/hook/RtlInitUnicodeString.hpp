#ifndef APPBOX_SANDBOX_HOOK_RTLINITUNICODESTRING_HPP
#define APPBOX_SANDBOX_HOOK_RTLINITUNICODESTRING_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-rtlinitunicodestring
 */
typedef void (*T_RtlInitUnicodeString)(PUNICODE_STRING DestinationString, PCWSTR SourceString);

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
void InjectRtlInitUnicodeString();

} // namespace appbox

#endif
