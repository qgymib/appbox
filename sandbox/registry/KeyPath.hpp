#ifndef APPBOX_SANDBOX_REGISTRY_KEYPATH_HPP
#define APPBOX_SANDBOX_REGISTRY_KEYPATH_HPP

#include <string>

namespace appbox
{
namespace registry
{

/**
 * @brief Strip a registry key path prefix, respecting the component boundary.
 *
 * The comparison is case insensitive. The prefix matches only when the path is
 * equal to the prefix or continues with a component separator, so the prefix
 * `\REGISTRY\USER\S-1-2` does not match `\REGISTRY\USER\S-1-22\Software`.
 *
 * @param[in] path The full registry key path.
 * @param[in] prefix The prefix to strip.
 * @param[out] relative The remaining path without the prefix and without the
 *                     leading separator. Empty when the path equals the prefix.
 * @return Whether the prefix matched.
 */
bool StripKeyPrefix(const std::wstring& path, const std::wstring& prefix, std::wstring& relative);

/**
 * @brief Join a registry key path and a relative key name.
 * @param[in] root The root key path, without trailing separator.
 * @param[in] name The relative key name, without separators around it.
 * @return The joined path. Empty operands are handled gracefully.
 */
std::wstring JoinKeyPath(const std::wstring& root, const std::wstring& name);

} // namespace registry
} // namespace appbox

#endif // APPBOX_SANDBOX_REGISTRY_KEYPATH_HPP
