#ifndef APPBOX_SANDBOX_HOOK_RTLCOMPAREUNICODESTRING_HPP
#define APPBOX_SANDBOX_HOOK_RTLCOMPAREUNICODESTRING_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-rtlcompareunicodestring
 */
/* clang-format off */
typedef LONG (*T_RtlCompareUnicodeString)(
    /* [IN} */  PCUNICODE_STRING    String1,
    /* [IN} */  PCUNICODE_STRING    String2,
    /* [IN} */  BOOLEAN             CaseInSensitive
);
/* clang-format on */

/**
 * @brief RtlCompareUnicodeString() direct call.
 */
extern T_RtlCompareUnicodeString sys_RtlCompareUnicodeString;
}

namespace appbox
{
    
/**
 * @brief Hook RtlCompareUnicodeString().
 */
extern HookRecord HookRtlCompareUnicodeString;

} // namespace appbox

#endif
