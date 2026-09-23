#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "registry/Whiteout.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>

/**
 * @brief The marker key of a deleted key lives in the key namespace of the store.
 */
TEST(UnitRegistryWhiteout, KeyPath)
{
    ASSERT_EQ(appbox::registry::WhiteoutKeyPath(L"HKEY_CURRENT_USER\\Software\\Vendor"),
              L"APPBOX_WHITEOUT\\K\\HKEY_CURRENT_USER\\Software\\Vendor");

    /* A root key of the view is a key like every other one. */
    ASSERT_EQ(appbox::registry::WhiteoutKeyPath(L"HKEY_LOCAL_MACHINE"), L"APPBOX_WHITEOUT\\K\\HKEY_LOCAL_MACHINE");

    /* Without a key the namespace itself is addressed. */
    ASSERT_EQ(appbox::registry::WhiteoutKeyPath(L""), L"APPBOX_WHITEOUT\\K");
}

/**
 * @brief The marker key of deleted values lives in the value namespace of the
 *        store, so a deleted key and a deleted value never collide.
 */
TEST(UnitRegistryWhiteout, ValuePath)
{
    ASSERT_EQ(appbox::registry::WhiteoutValueKeyPath(L"HKEY_CURRENT_USER\\Software\\Vendor"),
              L"APPBOX_WHITEOUT\\V\\HKEY_CURRENT_USER\\Software\\Vendor");

    ASSERT_EQ(appbox::registry::WhiteoutValueKeyPath(L""), L"APPBOX_WHITEOUT\\V");

    /* The two namespaces are disjoint for every path. */
    ASSERT_NE(appbox::registry::WhiteoutKeyPath(L"HKEY_CURRENT_USER"),
              appbox::registry::WhiteoutValueKeyPath(L"HKEY_CURRENT_USER"));
}

/**
 * @brief The prefixes walk from the key itself up to its first component.
 */
TEST(UnitRegistryWhiteout, KeyPathPrefixes)
{
    std::vector<std::wstring> prefixes;

    appbox::registry::KeyPathPrefixes(L"A\\B\\C", prefixes);
    ASSERT_EQ(prefixes.size(), 3u);
    ASSERT_EQ(prefixes[0], L"A\\B\\C");
    ASSERT_EQ(prefixes[1], L"A\\B");
    ASSERT_EQ(prefixes[2], L"A");

    appbox::registry::KeyPathPrefixes(L"Single", prefixes);
    ASSERT_EQ(prefixes.size(), 1u);
    ASSERT_EQ(prefixes[0], L"Single");

    /* A path of the view always starts with a root key, which is a prefix too. */
    appbox::registry::KeyPathPrefixes(L"HKEY_CURRENT_USER\\Software", prefixes);
    ASSERT_EQ(prefixes.size(), 2u);
    ASSERT_EQ(prefixes[0], L"HKEY_CURRENT_USER\\Software");
    ASSERT_EQ(prefixes[1], L"HKEY_CURRENT_USER");
}

/**
 * @brief An empty path yields no prefix and clears the output.
 */
TEST(UnitRegistryWhiteout, KeyPathPrefixesEmpty)
{
    std::vector<std::wstring> prefixes = {L"Stale"};

    appbox::registry::KeyPathPrefixes(L"", prefixes);
    ASSERT_TRUE(prefixes.empty());
}

/**
 * @brief A doubled separator is not normalised: the walk splits at the last
 *        separator of every step.
 *
 * The intermediate prefix ends with a separator, which is a path no marker of
 * the store can carry (a marker path is built from a key path of the view), so
 * the extra step of the walk never matches anything. The lookup stays correct
 * and the walk stays a plain string operation.
 */
TEST(UnitRegistryWhiteout, KeyPathPrefixesDoubledSeparator)
{
    std::vector<std::wstring> prefixes;

    appbox::registry::KeyPathPrefixes(L"A\\\\B", prefixes);
    ASSERT_EQ(prefixes.size(), 3u);
    ASSERT_EQ(prefixes[0], L"A\\\\B");
    ASSERT_EQ(prefixes[1], L"A\\");
    ASSERT_EQ(prefixes[2], L"A");
}
