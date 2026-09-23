#include "registry/RootMap.hpp"
#include <gtest/gtest.h>
#include <string>

namespace
{

/** NT path prefix of the HKCU of the current user used by the tests. */
const std::wstring kHkcu = L"\\REGISTRY\\USER\\S-1-5-21-1000";

/**
 * @brief Map a view path and require success.
 * @param[in] view_path The NT path to map.
 * @param[in] hkcu_prefix The HKCU prefix of the test.
 * @return The hive relative path.
 */
std::wstring ToHive(const std::wstring& view_path, const std::wstring& hkcu_prefix = kHkcu)
{
    std::wstring relative;
    EXPECT_TRUE(appbox::registry::MapViewPathToHive(view_path, hkcu_prefix, relative)) << view_path;
    return relative;
}

/**
 * @brief Map a hive relative path and require success.
 * @param[in] relative The hive relative path to map.
 * @param[in] hkcu_prefix The HKCU prefix of the test.
 * @return The NT path of the view.
 */
std::wstring ToView(const std::wstring& relative, const std::wstring& hkcu_prefix = kHkcu)
{
    std::wstring view_path;
    EXPECT_TRUE(appbox::registry::MapHivePathToView(relative, hkcu_prefix, view_path)) << relative;
    return view_path;
}

} // namespace

/**
 * @brief Every root key of the view maps onto its own sub key of the hive.
 */
TEST(UnitRegistryRootMap, ViewPathToHive)
{
    ASSERT_EQ(ToHive(L"\\REGISTRY\\USER\\S-1-5-21-1000"), L"HKEY_CURRENT_USER");
    ASSERT_EQ(ToHive(L"\\REGISTRY\\USER\\S-1-5-21-1000\\Software\\Vendor"),
              L"HKEY_CURRENT_USER\\Software\\Vendor");

    ASSERT_EQ(ToHive(L"\\REGISTRY\\MACHINE"), L"HKEY_LOCAL_MACHINE");
    ASSERT_EQ(ToHive(L"\\REGISTRY\\MACHINE\\SOFTWARE\\Vendor"), L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Vendor");

    ASSERT_EQ(ToHive(L"\\REGISTRY\\USER"), L"HKEY_USERS");
    ASSERT_EQ(ToHive(L"\\REGISTRY\\USER\\S-1-5-21-2000\\Software"), L"HKEY_USERS\\S-1-5-21-2000\\Software");

    ASSERT_EQ(ToHive(L"\\REGISTRY\\MACHINE\\SOFTWARE\\CLASSES\\CLSID"), L"HKEY_CLASSES_ROOT\\CLSID");

    ASSERT_EQ(ToHive(L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET\\HARDWARE PROFILES\\CURRENT\\Foo"),
              L"HKEY_CURRENT_CONFIG\\Foo");
}

/**
 * @brief The longest matching prefix wins, so HKCR and HKCC are not answered
 *        by the machine root they live below.
 */
TEST(UnitRegistryRootMap, ViewPathToHiveLongestPrefix)
{
    ASSERT_EQ(ToHive(L"\\REGISTRY\\MACHINE\\SOFTWARE\\CLASSES"), L"HKEY_CLASSES_ROOT");
    ASSERT_EQ(ToHive(L"\\REGISTRY\\MACHINE\\SOFTWARE\\ClassesX"), L"HKEY_LOCAL_MACHINE\\SOFTWARE\\ClassesX");

    ASSERT_EQ(ToHive(L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET\\HARDWARE PROFILES\\CURRENT"),
              L"HKEY_CURRENT_CONFIG");
}

/**
 * @brief The current user root wins over the HKEY_USERS prefix.
 */
TEST(UnitRegistryRootMap, CurrentUserWinsOverUsers)
{
    ASSERT_EQ(ToHive(L"\\REGISTRY\\USER\\S-1-5-21-1000\\Software"), L"HKEY_CURRENT_USER\\Software");

    /* A user with a longer SID is not the current user. */
    ASSERT_EQ(ToHive(L"\\REGISTRY\\USER\\S-1-5-21-1000-extra\\Software"),
              L"HKEY_USERS\\S-1-5-21-1000-extra\\Software");
}

/**
 * @brief The comparison ignores the case, the rest of the path is kept.
 */
TEST(UnitRegistryRootMap, ViewPathToHiveIgnoreCase)
{
    ASSERT_EQ(ToHive(L"\\registry\\machine\\Software\\Vendor"), L"HKEY_LOCAL_MACHINE\\Software\\Vendor");
    ASSERT_EQ(ToHive(L"\\Registry\\User\\s-1-5-21-1000\\Software"), L"HKEY_CURRENT_USER\\Software");
}

/**
 * @brief A prefix has to end on a component boundary.
 */
TEST(UnitRegistryRootMap, ViewPathToHiveBoundary)
{
    std::wstring relative;

    ASSERT_FALSE(appbox::registry::MapViewPathToHive(L"\\REGISTRY\\MACHINEX\\Software", kHkcu, relative));
    ASSERT_FALSE(appbox::registry::MapViewPathToHive(L"\\REGISTRY\\USERS", kHkcu, relative));

    /* The longer SID is not the current user, but it is still inside HKEY_USERS. */
    ASSERT_TRUE(appbox::registry::MapViewPathToHive(L"\\REGISTRY\\USER\\S-1-5-21-1000x", kHkcu, relative));
    ASSERT_EQ(relative, L"HKEY_USERS\\S-1-5-21-1000x");
}

/**
 * @brief Paths outside the view, like the private application hive mounts, are
 *        not isolated.
 */
TEST(UnitRegistryRootMap, ViewPathToHiveNotIsolated)
{
    std::wstring relative;

    ASSERT_FALSE(appbox::registry::MapViewPathToHive(L"\\REGISTRY\\A\\{00000000-0000-0000-0000-000000000000}",
                                                     kHkcu, relative));
    ASSERT_FALSE(appbox::registry::MapViewPathToHive(L"\\Device\\HarddiskVolume1\\Windows", kHkcu, relative));
    ASSERT_FALSE(appbox::registry::MapViewPathToHive(L"", kHkcu, relative));
}

/**
 * @brief Without the SID of the user the current user root cannot be
 *        recognized, so the path falls back to HKEY_USERS.
 */
TEST(UnitRegistryRootMap, ViewPathToHiveWithoutHkcuPrefix)
{
    std::wstring relative;

    ASSERT_TRUE(appbox::registry::MapViewPathToHive(L"\\REGISTRY\\USER\\S-1-5-21-1000\\Software", L"", relative));
    ASSERT_EQ(relative, L"HKEY_USERS\\S-1-5-21-1000\\Software");

    /* The other roots do not need the SID. */
    ASSERT_TRUE(appbox::registry::MapViewPathToHive(L"\\REGISTRY\\MACHINE\\SOFTWARE", L"", relative));
    ASSERT_EQ(relative, L"HKEY_LOCAL_MACHINE\\SOFTWARE");
}

/**
 * @brief The hive relative path maps back onto the NT path of the view.
 */
TEST(UnitRegistryRootMap, HivePathToView)
{
    ASSERT_EQ(ToView(L"HKEY_CURRENT_USER"), kHkcu);
    ASSERT_EQ(ToView(L"HKEY_CURRENT_USER\\Software\\Vendor"), kHkcu + L"\\Software\\Vendor");
    ASSERT_EQ(ToView(L"HKEY_LOCAL_MACHINE"), L"\\REGISTRY\\MACHINE");
    ASSERT_EQ(ToView(L"HKEY_USERS\\S-1-5-21-2000"), L"\\REGISTRY\\USER\\S-1-5-21-2000");
    ASSERT_EQ(ToView(L"HKEY_CLASSES_ROOT\\CLSID"), L"\\REGISTRY\\MACHINE\\SOFTWARE\\CLASSES\\CLSID");
    ASSERT_EQ(ToView(L"HKEY_CURRENT_CONFIG\\Foo"),
              L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET\\HARDWARE PROFILES\\CURRENT\\Foo");
}

/**
 * @brief The reverse mapping ignores the case and rejects unknown roots.
 */
TEST(UnitRegistryRootMap, HivePathToViewIgnoreCaseAndUnknown)
{
    ASSERT_EQ(ToView(L"Hkey_Current_User\\software"), kHkcu + L"\\software");

    std::wstring view_path;
    ASSERT_FALSE(appbox::registry::MapHivePathToView(L"HKEY_FOO\\Bar", kHkcu, view_path));
    ASSERT_FALSE(appbox::registry::MapHivePathToView(L"", kHkcu, view_path));
    ASSERT_FALSE(appbox::registry::MapHivePathToView(L"HKEY_CURRENT_USER\\Software", L"", view_path));
}

/**
 * @brief The round trip through both directions keeps the path.
 */
TEST(UnitRegistryRootMap, RoundTrip)
{
    const std::wstring paths[] = {
        L"\\REGISTRY\\MACHINE\\SOFTWARE\\Vendor",
        L"\\REGISTRY\\USER\\S-1-5-21-1000\\Software\\Vendor",
        L"\\REGISTRY\\USER\\S-1-5-21-2000\\Software",
        L"\\REGISTRY\\MACHINE\\SOFTWARE\\CLASSES\\CLSID",
        L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET\\HARDWARE PROFILES\\CURRENT\\Foo",
    };

    for (const auto& path : paths)
    {
        std::wstring relative;
        ASSERT_TRUE(appbox::registry::MapViewPathToHive(path, kHkcu, relative)) << path;

        std::wstring view_path;
        ASSERT_TRUE(appbox::registry::MapHivePathToView(relative, kHkcu, view_path)) << relative;
        ASSERT_EQ(view_path, path);
    }
}

/**
 * @brief The root key name of a hive path is reported in its canonical form.
 */
TEST(UnitRegistryRootMap, HiveRootKeyName)
{
    ASSERT_EQ(appbox::registry::HiveRootKeyName(L"Hkey_Current_User\\Software"), L"HKEY_CURRENT_USER");
    ASSERT_EQ(appbox::registry::HiveRootKeyName(L"HKEY_LOCAL_MACHINE"), L"HKEY_LOCAL_MACHINE");
    ASSERT_EQ(appbox::registry::HiveRootKeyName(L"HKEY_USERS\\S-1-5-21-2000"), L"HKEY_USERS");
    ASSERT_EQ(appbox::registry::HiveRootKeyName(L"HKEY_FOO\\Bar"), L"");
    ASSERT_EQ(appbox::registry::HiveRootKeyName(L""), L"");
}

/**
 * @brief The hive carries one sub key per root key of the view.
 */
TEST(UnitRegistryRootMap, HiveRootKeyNames)
{
    const auto& names = appbox::registry::HiveRootKeyNames();
    ASSERT_EQ(names.size(), static_cast<std::size_t>(5));

    EXPECT_EQ(names[0], L"HKEY_CLASSES_ROOT");
    EXPECT_EQ(names[1], L"HKEY_CURRENT_USER");
    EXPECT_EQ(names[2], L"HKEY_LOCAL_MACHINE");
    EXPECT_EQ(names[3], L"HKEY_USERS");
    EXPECT_EQ(names[4], L"HKEY_CURRENT_CONFIG");

    /* Every name has to be reachable through the view mapping as well. */
    for (const auto& name : names)
    {
        std::wstring view_path;
        ASSERT_TRUE(appbox::registry::MapHivePathToView(name, kHkcu, view_path)) << name;
        ASSERT_FALSE(view_path.empty());
    }
}

/**
 * @brief The fixed prefix table is ordered by descending prefix length.
 */
TEST(UnitRegistryRootMap, PrefixTableOrder)
{
    const auto& prefixes = appbox::registry::RootKeyPrefixes();
    ASSERT_FALSE(prefixes.empty());

    for (std::size_t index = 1; index < prefixes.size(); ++index)
    {
        const std::wstring previous = prefixes[index - 1].nt_prefix;
        const std::wstring current = prefixes[index].nt_prefix;
        ASSERT_GT(previous.size(), current.size()) << previous << " / " << current;
    }
}
