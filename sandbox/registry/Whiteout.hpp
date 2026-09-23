#ifndef APPBOX_SANDBOX_REGISTRY_WHITEOUT_HPP
#define APPBOX_SANDBOX_REGISTRY_WHITEOUT_HPP

#include "RegistryIsolation.hpp"
#include <string>
#include <vector>

namespace appbox
{
namespace registry
{

/**
 * @brief Paths of the whiteout store of the sandbox hive.
 *
 * A delete inside the sandbox never reaches the host registry, so it has to be
 * recorded instead: the deleted entry is written into the whiteout store of
 * the hive (see `common/RegistryIsolation.hpp` for the layout) and every read
 * of the merged view consults the store, so the host entry stays invisible.
 *
 * The helpers only build and split paths, which keeps them free of the
 * registry API and unit testable:
 *
 * ```
 * WhiteoutKeyPath(L"HKEY_CURRENT_USER\\Software\\Vendor")
 *     -> L"APPBOX_WHITEOUT\\K\\HKEY_CURRENT_USER\\Software\\Vendor"
 * WhiteoutValueKeyPath(L"HKEY_CURRENT_USER\\Software\\Vendor")
 *     -> L"APPBOX_WHITEOUT\\V\\HKEY_CURRENT_USER\\Software\\Vendor"
 * ```
 *
 * A deleted value is not a path component but a **value name** of the marker
 * key of `WhiteoutValueKeyPath()`, because a value name may contain a
 * backslash and would otherwise be indistinguishable from a nested path. The
 * empty name addresses the default value of the key like everywhere else.
 */

/**
 * @brief Path of the whiteout marker key of a key of the view.
 *
 * The presence of the key means that the key was deleted inside the sandbox:
 * the key and everything below it stay invisible for the merged view, no
 * matter which layer holds them.
 *
 * @param[in] relative The hive relative path of the key, for example
 *                     `HKEY_CURRENT_USER\Software\Vendor`.
 * @return The hive relative path of the marker key.
 */
std::wstring WhiteoutKeyPath(const std::wstring& relative);

/**
 * @brief Path of the whiteout marker key which holds the deleted value names.
 *
 * The marker key carries one value per deleted value name of the key; the
 * value names are the names of the deleted values and the empty name is the
 * default value of the key.
 *
 * @param[in] relative The hive relative path of the key which held the values,
 *                     for example `HKEY_CURRENT_USER\Software\Vendor`.
 * @return The hive relative path of the marker key.
 */
std::wstring WhiteoutValueKeyPath(const std::wstring& relative);

/**
 * @brief Split a key path into the path itself and every ancestor path.
 *
 * The prefixes are ordered longest first, which is the order a whiteout lookup
 * has to walk: a deleted key hides its whole subtree, so the lookup stops at
 * the first prefix which the store holds.
 *
 * ```
 * KeyPathPrefixes(L"A\\B\\C") -> { L"A\\B\\C", L"A\\B", L"A" }
 * ```
 *
 * @param[in] relative The hive relative path of the key.
 * @param[out] prefixes The prefixes, longest first, cleared first. An empty
 *                      path yields no prefix at all.
 */
void KeyPathPrefixes(const std::wstring& relative, std::vector<std::wstring>& prefixes);

} // namespace registry
} // namespace appbox

#endif // APPBOX_SANDBOX_REGISTRY_WHITEOUT_HPP
