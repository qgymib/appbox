#include <gtest/gtest.h>
#include "utils/KnownFolder.hpp"
#include "WString.hpp"
#include <iterator>
#include <string>
#include <vector>

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

TEST(Unit_ExpandKnownFolder, PlainPathIsReturnedUnchanged)
{
    EXPECT_EQ(appbox::ExpandKnownFolder(L""), L"");
    EXPECT_EQ(appbox::ExpandKnownFolder(L"C:\\Apps\\run.exe"), L"C:\\Apps\\run.exe");
}

TEST(Unit_ExpandKnownFolder, TokenPrefixIsExpanded)
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

TEST(Unit_ExpandKnownFolder, TokenWithoutRemainderHasNoTrailingSeparator)
{
    const auto   expanded = appbox::ExpandKnownFolder(L"#ProgramFiles#");
    std::wstring folder;
    ASSERT_TRUE(appbox::SearchFolderID(L"#ProgramFiles#", folder));
    EXPECT_EQ(expanded, folder);
}

TEST(Unit_ExpandKnownFolder, UnknownTokenIsReturnedUnchanged)
{
    EXPECT_EQ(appbox::ExpandKnownFolder(L"#NoSuchFolder#\\x"), L"#NoSuchFolder#\\x");
}

/**
 * @brief The former `%Name%` delimited form is not a layer key any more, so a
 *        path which uses it stays untouched and is not resolvable.
 */
TEST(Unit_ExpandKnownFolder, PercentDelimitedTokenIsNotALayerKey)
{
    EXPECT_EQ(appbox::ExpandKnownFolder(L"%ProgramFiles%\\MyApp\\app.exe"), L"%ProgramFiles%\\MyApp\\app.exe");

    std::wstring folder;
    EXPECT_FALSE(appbox::SearchFolderID(L"%ProgramFiles%", folder));
}

/**
 * @brief Every layer key is `#` delimited and free of the shell escape
 *        character, which is what keeps `%%` out of the packed paths.
 */
TEST(Unit_ExpandKnownFolder, LayerKeysUseTheHashDelimiter)
{
    const std::wstring keys[] = {
        L"#ProgramFiles#", L"#ProgramFilesCommon#", L"#USERPROFILE#", L"#Documents#",   L"#Desktop#", L"#AppData#",
        L"#LocalAppData#", L"#LocalAppDataLow#",    L"#Downloads#",   L"#Favorites#",   L"#Music#",   L"#Pictures#",
        L"#StartMenu#",    L"#Programs#",           L"#Startup#",     L"#ProgramData#", L"#Windows#", L"#System32#",
        L"#Fonts#"
    };

    for (const auto& key : keys)
    {
        EXPECT_EQ(key.front(), L'#');
        EXPECT_EQ(key.back(), L'#');
        EXPECT_EQ(key.find(L'%'), std::wstring::npos);

        std::wstring folder;
        EXPECT_TRUE(appbox::SearchFolderID(key, folder));
    }
}

/**
 * @brief Only the layer keys the packer produces are known.
 *
 * A mapping which no preset directory uses describes no layer of the archive,
 * so it was removed: a layer directory named after such a key is rejected by
 * `MapBaseFS`, and a startup file path which uses it is not expanded. A key is
 * compared exactly, so the historical `#APPDATA#` is not the `#AppData#` of the
 * current table.
 */
TEST(Unit_ExpandKnownFolder, RemovedLayerKeysAreNotKnown)
{
    const std::wstring keys[] = { L"#APPDATA#", L"#windir#", L"#Profile#", L"#RoamingAppData#", L"#ALLUSERSPROFILE#" };

    for (const auto& key : keys)
    {
        std::wstring folder;
        EXPECT_FALSE(appbox::SearchFolderID(key, folder));
        EXPECT_EQ(appbox::ExpandKnownFolder(key + L"\\MyApp\\app.exe"), key + L"\\MyApp\\app.exe");
    }
}

/**
 * @brief The variables the sandbox expands in the values of the workspace
 *        follow the table of the known folders.
 *
 * The name of a variable is the layer key of the folder without its `#`
 * delimiters, so a preset directory which is added to the packer later brings
 * its variable with it instead of having to be listed twice.
 */
TEST(Unit_ExpandKnownFolder, VariablesFollowTheKnownFolderTable)
{
    const std::wstring keys[] = {
        L"#ProgramFiles#", L"#ProgramFilesCommon#", L"#USERPROFILE#", L"#Documents#",   L"#Desktop#", L"#AppData#",
        L"#LocalAppData#", L"#LocalAppDataLow#",    L"#Downloads#",   L"#Favorites#",   L"#Music#",   L"#Pictures#",
        L"#StartMenu#",    L"#Programs#",           L"#Startup#",     L"#ProgramData#", L"#Windows#", L"#System32#",
        L"#Fonts#"
    };
    const char* names[] = { "ProgramFiles", "ProgramFilesCommon", "USERPROFILE", "Documents",   "Desktop", "AppData",
                            "LocalAppData", "LocalAppDataLow",    "Downloads",   "Favorites",   "Music",   "Pictures",
                            "StartMenu",    "Programs",           "Startup",     "ProgramData", "Windows", "System32",
                            "Fonts" };

    const std::vector<appbox::KnownFolderVariable> variables = appbox::KnownFolderVariables();
    ASSERT_EQ(variables.size(), std::size(keys));
    ASSERT_EQ(variables.size(), static_cast<std::size_t>(19));

    for (std::size_t index = 0; index < std::size(keys); ++index)
    {
        EXPECT_EQ(variables[index].name, names[index]);

        /* The path of the variable is the real folder of this machine. */
        std::wstring folder;
        ASSERT_TRUE(appbox::SearchFolderID(keys[index], folder));
        EXPECT_EQ(variables[index].path, appbox::WideToUTF8(folder));
    }
}
