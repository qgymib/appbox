#ifndef APPBOX_SANDBOX_HOOK_NTCREATEFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTCREATEFILE_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"
#include <nlohmann/json.hpp>

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntcreatefile
 */
/* clang-format off */
typedef NTSTATUS (*T_NtCreateFile)(
    /* [OUT] */         PHANDLE             FileHandle,
    /* [IN] */          ACCESS_MASK         DesiredAccess,
    /* [IN] */          POBJECT_ATTRIBUTES  ObjectAttributes,
    /* [OUT] */         PIO_STATUS_BLOCK    IoStatusBlock,
    /* [IN,OPTIONAL] */ PLARGE_INTEGER      AllocationSize,
    /* [IN] */          ULONG               FileAttributes,
    /* [IN] */          ULONG               ShareAccess,
    /* [IN] */          ULONG               CreateDisposition,
    /* [IN] */          ULONG               CreateOptions,
    /* [IN] */          PVOID               EaBuffer,
    /* [IN] */          ULONG               EaLength
);
/* clang-format on */

/**
 * @brief NtCreateFile() direct call.
 */
extern T_NtCreateFile sys_NtCreateFile;
}

namespace appbox
{

struct NtCreateFileLock
{
    NtCreateFileLock();
    ~NtCreateFileLock();
};

/**
 * @brief Hook NtCreateFile().
 */
extern HookRecord HookNtCreateFile;

/**
 * @brief Convert POBJECT_ATTRIBUTES to full NT path.
 * @param[in] ObjectAttributes Object attributes.
 * @param[in] CreateOptions Create options.  Only `FILE_OPEN_BY_FILE_ID` matters.
 * @param[out] path Full NT path.
 * @return True if success.
 */
bool ConvertToFullNtPath(const POBJECT_ATTRIBUTES ObjectAttributes, ULONG CreateOptions, std::wstring& path);

/**
 * @brief Convert POBJECT_ATTRIBUTES to json.
 * @param[in] ObjectAttributes Object attributes.
 * @param[in] CreateOptions Create options. Only `FILE_OPEN_BY_FILE_ID` matters.
 * @return Json object.
 */
nlohmann::json ToJson(const POBJECT_ATTRIBUTES ObjectAttributes, ULONG CreateOptions);

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_NTCREATEFILE_HPP
