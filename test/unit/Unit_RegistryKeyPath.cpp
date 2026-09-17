#include "registry/KeyPath.hpp"
#include <gtest/gtest.h>

/**
 * @brief A prefix on the exact path matches and yields an empty relative path.
 */
TEST(UnitRegistryKeyPath, StripKeyPrefixExactMatch)
{
    std::wstring relative = L"<invalid>";
    ASSERT_TRUE(appbox::registry::StripKeyPrefix(L"\\REGISTRY\\USER\\S-1-5-21", L"\\REGISTRY\\USER\\S-1-5-21",
                                                 relative));
    ASSERT_EQ(relative, L"");
}

/**
 * @brief A prefix matches the first components and yields the rest of the path.
 */
TEST(UnitRegistryKeyPath, StripKeyPrefixChild)
{
    std::wstring relative;
    ASSERT_TRUE(appbox::registry::StripKeyPrefix(L"\\REGISTRY\\USER\\S-1-5-21\\Software\\AppBox",
                                                L"\\REGISTRY\\USER\\S-1-5-21", relative));
    ASSERT_EQ(relative, L"Software\\AppBox");
}

/**
 * @brief The comparison is case insensitive.
 */
TEST(UnitRegistryKeyPath, StripKeyPrefixIgnoreCase)
{
    std::wstring relative;
    ASSERT_TRUE(appbox::registry::StripKeyPrefix(L"\\registry\\user\\S-1-5-21\\Software",
                                                 L"\\REGISTRY\\USER\\S-1-5-21", relative));
    ASSERT_EQ(relative, L"Software");
}

/**
 * @brief The prefix must end on a component boundary.
 */
TEST(UnitRegistryKeyPath, StripKeyPrefixBoundary)
{
    std::wstring relative;
    ASSERT_FALSE(appbox::registry::StripKeyPrefix(L"\\REGISTRY\\USER\\S-1-5-212\\Software",
                                                  L"\\REGISTRY\\USER\\S-1-5-21", relative));

    ASSERT_FALSE(appbox::registry::StripKeyPrefix(L"\\REGISTRY\\USERS-1-5-21", L"\\REGISTRY\\USER\\S-1-5-21",
                                                   relative));
}

/**
 * @brief A path of another user does not match the HKCU prefix.
 */
TEST(UnitRegistryKeyPath, StripKeyPrefixOtherUser)
{
    std::wstring relative;
    ASSERT_FALSE(appbox::registry::StripKeyPrefix(L"\\REGISTRY\\USER\\S-1-5-21-OTHER\\Software",
                                                  L"\\REGISTRY\\USER\\S-1-5-21-MINE", relative));

    ASSERT_FALSE(appbox::registry::StripKeyPrefix(L"\\REGISTRY\\MACHINE\\Software", L"\\REGISTRY\\USER\\S-1-5-21",
                                                   relative));
}

/**
 * @brief A prefix longer than the path never matches.
 */
TEST(UnitRegistryKeyPath, StripKeyPrefixLongerPrefix)
{
    std::wstring relative;
    ASSERT_FALSE(appbox::registry::StripKeyPrefix(L"\\REGISTRY\\USER", L"\\REGISTRY\\USER\\S-1-5-21", relative));
}

/**
 * @brief A path with a trailing separator still matches and keeps the content.
 */
TEST(UnitRegistryKeyPath, StripKeyPrefixTrailingSeparator)
{
    std::wstring relative;
    ASSERT_TRUE(appbox::registry::StripKeyPrefix(L"\\REGISTRY\\USER\\S-1-5-21\\Software\\", L"\\REGISTRY\\USER\\S-1-5-21",
                                                 relative));
    ASSERT_EQ(relative, L"Software\\");
}

/**
 * @brief Join handles every combination of empty operands.
 */
TEST(UnitRegistryKeyPath, JoinKeyPath)
{
    ASSERT_EQ(appbox::registry::JoinKeyPath(L"", L""), L"");
    ASSERT_EQ(appbox::registry::JoinKeyPath(L"", L"Software"), L"Software");
    ASSERT_EQ(appbox::registry::JoinKeyPath(L"\\REGISTRY\\USER\\S-1-5-21", L""), L"\\REGISTRY\\USER\\S-1-5-21");
    ASSERT_EQ(appbox::registry::JoinKeyPath(L"\\REGISTRY\\USER\\S-1-5-21", L"Software\\AppBox"),
              L"\\REGISTRY\\USER\\S-1-5-21\\Software\\AppBox");
}
