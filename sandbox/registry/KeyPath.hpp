#ifndef APPBOX_SANDBOX_REGISTRY_KEYPATH_HPP
#define APPBOX_SANDBOX_REGISTRY_KEYPATH_HPP

#include <string>
#include <vector>

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

/**
 * @brief The path of the parent key of a registry key path.
 *
 * The helper names the key which holds the last component of a path, which is
 * the key a rename of the last component takes place in.
 *
 * ```
 * ParentKeyPath(L"HKEY_CURRENT_USER\\Software\\Vendor") -> L"HKEY_CURRENT_USER\\Software"
 * ```
 *
 * @param[in] path The key path, with or without a leading separator.
 * @return The parent path, empty when the path has no separator and therefore
 *         no parent component.
 */
std::wstring ParentKeyPath(const std::wstring& path);

/**
 * @brief Split a registry key path into its components.
 *
 * The components are separated by backslashes. Empty components — a doubled
 * separator, a leading or a trailing one — are skipped, so
 * `HKEY_CURRENT_USER\Software\App` yields three components and
 * `HKEY_CURRENT_USER\` yields one.
 *
 * @param[in] path The registry key path.
 * @param[out] components The components in order, cleared first.
 * @return true when the path holds at least one component.
 */
bool SplitKeyPath(const std::wstring& path, std::vector<std::wstring>& components);

} // namespace registry
} // namespace appbox

#endif // APPBOX_SANDBOX_REGISTRY_KEYPATH_HPP
