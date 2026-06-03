#ifndef APPBOX_SANDBOX_HOOK_NTDELETEFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTDELETEFILE_HPP

#include "utils/WinAPI.h"
#include "filesystem/Resolve.hpp"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwdeletefile
 */
/* clang-format off */
typedef NTSTATUS (*T_NtDeleteFile)(
    /* [IN] */  POBJECT_ATTRIBUTES  ObjectAttributes
);
/* clang-format on */

/**
 * @brief NtDeleteFile() direct call.
 */
extern T_NtDeleteFile sys_NtDeleteFile;
}

namespace appbox
{

/**
 * @brief Hook NtDeleteFile().
 */
extern HookRecord HookNtDeleteFile;

/**
 * @brief Delete path in mapped view.
 * @param[in] path File or directory NT path in mapped view.
 * @param[in] Attributes Name attributes. Only `OBJ_CASE_INSENSITIVE` matters.
 * @return Status code.
 */
NTSTATUS DeleteViewPath(const std::wstring& path, ULONG Attributes);

/**
 * @brief Delete path in mapped view.
 * @param[in] resolve Resolve result.
 * @param[in] Attributes Name attributes. Only `OBJ_CASE_INSENSITIVE` matters.
 * @return Status code.
 */
NTSTATUS DeleteViewPath(const filesystem::ResolveResult& resolve, ULONG Attributes);

} // namespace appbox

#endif
