#ifndef APPBOX_LOADER_UTILS_WINCALL_HPP
#define APPBOX_LOADER_UTILS_WINCALL_HPP

#include "sandbox/utils/WinAPI.h"

extern "C" {
extern T_RtlDosPathNameToNtPathName_U_WithStatus sys_RtlDosPathNameToNtPathName_U_WithStatus;
extern T_RtlFreeUnicodeString                    sys_RtlFreeUnicodeString;
extern T_RtlNtStatusToDosError                   sys_RtlNtStatusToDosError;
}

namespace appbox
{

/**
 * @brief Initialize windows API.
 * @return 0 on success, otherwise error code.
 */
DWORD WinCallInit();

} // namespace appbox

#endif
