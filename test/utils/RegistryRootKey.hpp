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

/**
 * @brief Get the SID of the current user as a text.
 *
 * `HKEY_USERS` holds one sub key per user, which is named after the SID of that
 * user, so the SID is what a path through that root starts with. A test which
 * needs a handle of a root key of the view — `HKEY_CURRENT_USER` for example —
 * opens `HKEY_USERS\<SID>`, which the isolation resolves to the same key.
 *
 * @return The SID, empty when it cannot be read.
 */
std::wstring CurrentUserSid();

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_REGISTRY_ROOT_KEY_HPP
