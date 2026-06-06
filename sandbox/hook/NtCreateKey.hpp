#ifndef APPBOX_SANDBOX_HOOK_NTCREATEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTCREATEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"
#include <nlohmann/json.hpp>

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwcreatekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtCreateKey)(
    /* [OUT] */             PHANDLE             KeyHandle,
    /* [IN] */              ACCESS_MASK         DesiredAccess,
    /* [IN] */              POBJECT_ATTRIBUTES  ObjectAttributes,
    /* [IN] */              ULONG               TitleIndex,
    /* [IN,OPTIONAL] */     PUNICODE_STRING     Class,
    /* [IN] */              ULONG               CreateOptions,
    /* [OUT,OPTIONAL] */    PULONG              Disposition
);
/* clang-format on */

/**
 * @brief NtCreateKey() direct call.
 */
extern T_NtCreateKey sys_NtCreateKey;
}

namespace appbox
{

/**
 * @brief Hook NtCreateKey().
 */
extern HookRecord HookNtCreateKey;

/**
 * @brief Convert registry key DesiredAccess value to JSON.
 * @param[in] DesiredAccess DesiredAccess value
 * @return JSON object
 */
nlohmann::json RegistryKeyDesiredAccessToJson(ACCESS_MASK DesiredAccess);

/**
 * @brief Convert registry key CreateOptions value to JSON.
 * @param[in] CreateOptions CreateOptions value
 * @return JSON object
 */
nlohmann::json RegistryKeyCreateOptionsToJson(ULONG CreateOptions);

} // namespace appbox

#endif
