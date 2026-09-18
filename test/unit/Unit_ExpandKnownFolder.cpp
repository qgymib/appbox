#include <gtest/gtest.h>
#include "utils/KnownFolder.hpp"
#include <string>

namespace
{

/**
 * @brief Check that a string contains no doubled path separator.
 * @param[in] path The path to check.
 * @return true when the path has no `\\\\` sequence.
 */
bool NoDoubledSeparator(const std::wstring& path)
{
    return path.find(L"\\\\") == std::wstring::npos;
}

} // namespace

TEST(ExpandKnownFolder, PlainPathIsReturnedUnchanged)
{
    EXPECT_EQ(appbox::ExpandKnownFolder(L""), L"");
    EXPECT_EQ(appbox::ExpandKnownFolder(L"C:\\Apps\\run.exe"), L"C:\\Apps\\run.exe");
}

TEST(ExpandKnownFolder, TokenPrefixIsExpanded)
{
    const auto expanded = appbox::ExpandKnownFolder(L"#ProgramFiles#\\MyApp\\app.exe");
    EXPECT_NE(expanded, L"#ProgramFiles#\\MyApp\\app.exe");
    EXPECT_NE(expanded.find(L"MyApp\\app.exe"), std::wstring::npos);
    EXPECT_TRUE(NoDoubledSeparator(expanded));

    /* The expanded prefix must be the real folder path. */
    std::wstring folder;
    ASSERT_TRUE(appbox::SearchFolderID(L"#ProgramFiles#", folder));
    EXPECT_EQ(expanded.find(folder), static_cast<std::size_t>(0));
}

TEST(ExpandKnownFolder, TokenWithoutRemainderHasNoTrailingSeparator)
{
    const auto expanded = appbox::ExpandKnownFolder(L"#ProgramFiles#");
    std::wstring folder;
    ASSERT_TRUE(appbox::SearchFolderID(L"#ProgramFiles#", folder));
    EXPECT_EQ(expanded, folder);
}

TEST(ExpandKnownFolder, UnknownTokenIsReturnedUnchanged)
{
    EXPECT_EQ(appbox::ExpandKnownFolder(L"#NoSuchFolder#\\x"), L"#NoSuchFolder#\\x");
}

/**
 * @brief The former `%Name%` delimited form is not a layer key any more, so a
 *        path which uses it stays untouched and is not resolvable.
 */
TEST(ExpandKnownFolder, PercentDelimitedTokenIsNotALayerKey)
{
    EXPECT_EQ(appbox::ExpandKnownFolder(L"%ProgramFiles%\\MyApp\\app.exe"),
              L"%ProgramFiles%\\MyApp\\app.exe");

    std::wstring folder;
    EXPECT_FALSE(appbox::SearchFolderID(L"%ProgramFiles%", folder));
}

/**
 * @brief Every layer key is `#` delimited and free of the shell escape
 *        character, which is what keeps `%%` out of the packed paths.
 */
TEST(ExpandKnownFolder, LayerKeysUseTheHashDelimiter)
{
    const std::wstring keys[] = { L"#ProgramFiles#", L"#USERPROFILE#", L"#APPDATA#", L"#windir#" };

    for (const auto& key : keys)
    {
        EXPECT_EQ(key.front(), L'#');
        EXPECT_EQ(key.back(), L'#');
        EXPECT_EQ(key.find(L'%'), std::wstring::npos);

        std::wstring folder;
        EXPECT_TRUE(appbox::SearchFolderID(key, folder));
    }
}
