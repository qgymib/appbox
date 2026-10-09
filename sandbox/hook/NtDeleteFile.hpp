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
 *
 * The path of the view is needed as well: the delete of an alternate data
 * stream records the marker of that stream, and that marker is a stream of the
 * file which carries it, so the file has to be in the upper layer first.
 *
 * @param[in] resolve Resolve result.
 * @param[in] view_path Path of the view of the entry.
 * @param[in] Attributes Name attributes. Only `OBJ_CASE_INSENSITIVE` matters.
 * @return Status code.
 */
NTSTATUS DeleteViewPath(const filesystem::ResolveResult& resolve, const std::wstring& view_path, ULONG Attributes);

/**
 * @brief Record the delete of an entry of the view inside the upper layer.
 *
 * The entry disappears from the view while the host filesystem stays
 * untouched: the copy the upper layer holds is deleted and the whiteout marker
 * which hides the layers below it is written, which is the state a delete
 * leaves behind when the isolation of the path does not name the host layer.
 *
 * The helper is the tail of a rename as well: the object moved away from the
 * path it had, so every layer which still holds the old name has to be hidden.
 *
 * @param[in] path View path of the entry which is gone.
 * @param[in] Attributes Name attributes. Only `OBJ_CASE_INSENSITIVE` matters.
 * @return Status code.
 */
NTSTATUS HideViewPath(const std::wstring& path, ULONG Attributes);

} // namespace appbox

#endif
