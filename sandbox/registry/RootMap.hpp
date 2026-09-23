#ifndef APPBOX_SANDBOX_REGISTRY_ROOTMAP_HPP
#define APPBOX_SANDBOX_REGISTRY_ROOTMAP_HPP

#include <string>
#include <vector>

namespace appbox
{
namespace registry
{

/**
 * @brief NT path prefix of one of the fixed root keys of the registry view.
 *
 * The prefix is the kernel object path the Win32 root key resolves to, for
 * example `\REGISTRY\MACHINE` for `HKEY_LOCAL_MACHINE`.
 */
struct RootKeyPrefix
{
    /**
     * @brief Name of the root key, for example `HKEY_LOCAL_MACHINE`.
     */
    const wchar_t* root_name;

    /**
     * @brief NT path of the root key, for example `\REGISTRY\MACHINE`.
     */
    const wchar_t* nt_prefix;
};

/**
 * @brief The fixed root key prefixes of the registry view.
 *
 * `HKEY_CURRENT_USER` is not part of the list: its NT path carries the SID of
 * the current user, so it is passed to the mapping functions separately. The
 * list is ordered by descending prefix length, so the longest match wins:
 * `\REGISTRY\MACHINE\SOFTWARE\CLASSES` (`HKEY_CLASSES_ROOT`) is tested before
 * `\REGISTRY\MACHINE` (`HKEY_LOCAL_MACHINE`) and
 * `\REGISTRY\MACHINE\SYSTEM\...\CURRENT` (`HKEY_CURRENT_CONFIG`) before both.
 *
 * @return The prefixes in match order.
 */
const std::vector<RootKeyPrefix>& RootKeyPrefixes();

/**
 * @brief The names of the root keys of the view, in display order.
 *
 * The list holds the four fixed roots of RootKeyPrefixes() and
 * `HKEY_CURRENT_USER`, whose NT path carries the SID of the current user and
 * is therefore not part of the prefix table. The hive carries one sub key per
 * entry, so the list is what a fresh hive has to be filled with.
 *
 * @return The root key names.
 */
const std::vector<std::wstring>& HiveRootKeyNames();

/**
 * @brief Map an NT registry path onto the hive relative path of the view.
 *
 * The hive holds one sub key per root key, so the hive relative path is the
 * name of the root key followed by the rest of the path, for example
 * `\REGISTRY\MACHINE\SOFTWARE\Vendor` becomes
 * `HKEY_LOCAL_MACHINE\SOFTWARE\Vendor` and `\REGISTRY\USER\<SID>\Software`
 * becomes `HKEY_CURRENT_USER\Software`. The comparison is case insensitive
 * and respects the component boundary.
 *
 * @param[in] view_path The NT path of the key, for example
 *                      `\REGISTRY\USER\<SID>\Software`.
 * @param[in] hkcu_prefix The NT path of the HKCU of the sandboxed user,
 *                        `\REGISTRY\USER\<SID>`. May be empty, in which case
 *                        the current user root cannot be mapped.
 * @param[out] relative The hive relative path when the path is isolated.
 * @return true when the path belongs to one of the root keys of the view.
 */
bool MapViewPathToHive(const std::wstring& view_path, const std::wstring& hkcu_prefix, std::wstring& relative);

/**
 * @brief Map a hive relative path back onto the NT registry path of the view.
 *
 * This is the inverse of MapViewPathToHive() and is used to translate the
 * object names of handles which point into the hive back into the view, so a
 * redirected handle names the key it shadows.
 *
 * @param[in] relative The hive relative path, for example
 *                     `HKEY_CURRENT_USER\Software`.
 * @param[in] hkcu_prefix The NT path of the HKCU of the sandboxed user,
 *                        `\REGISTRY\USER\<SID>`. May be empty, in which case
 *                        the current user root cannot be translated.
 * @param[out] view_path The NT path of the key when the name is known.
 * @return true when the path names one of the root keys of the view.
 */
bool MapHivePathToView(const std::wstring& relative, const std::wstring& hkcu_prefix, std::wstring& view_path);

/**
 * @brief Get the name of the root key a hive relative path starts with.
 *
 * @param[in] relative The hive relative path, for example
 *                     `HKEY_LOCAL_MACHINE\SOFTWARE`.
 * @return The canonical name of the root key, empty when the path does not
 *         start with a known root key.
 */
std::wstring HiveRootKeyName(const std::wstring& relative);

} // namespace registry
} // namespace appbox

#endif // APPBOX_SANDBOX_REGISTRY_ROOTMAP_HPP
