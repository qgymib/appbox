#ifndef APPBOX_LOADER_UTILS_REGISTRY_BUILDER_HPP
#define APPBOX_LOADER_UTILS_REGISTRY_BUILDER_HPP

#include "sandbox/utils/WinAPI.h"
#include <string>
#include <list>

namespace appbox
{

/**
 * @brief Build registry contents into registry hive.
 * @param[in] hKey Handle of hive file.
 * @param[in] regs Content of registry file.
 * @return Error code.
 */
DWORD RegistryBuild(HKEY hKey, std::list<std::wstring> regs);

} // namespace appbox

#endif
