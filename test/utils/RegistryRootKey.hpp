#ifndef APPBOX_TEST_UTILS_REGISTRY_ROOT_KEY_HPP
#define APPBOX_TEST_UTILS_REGISTRY_ROOT_KEY_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <string>

namespace appbox::test
{

/**
 * @brief Resolve the predefined handle of a registry root key.
 *
 * The handle is what a probe passes to the registry API; the sandbox resolves
 * it to the NT path of the root key and redirects it into the hive.
 *
 * @param[in] name Name of the root key, for example `"HKEY_LOCAL_MACHINE"`.
 *                 An empty name resolves to `HKEY_CURRENT_USER`.
 * @return The predefined handle, nullptr for an unknown name.
 */
HKEY RegistryRootHandle(const std::string& name);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_REGISTRY_ROOT_KEY_HPP
