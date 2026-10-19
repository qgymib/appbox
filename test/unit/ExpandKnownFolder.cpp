#include <gtest/gtest.h>
#include "utils/KnownFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <algorithm>
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

/**
 * @brief Path a known folder has in a 32 bit process.
 *
 * The operating system redirects `ProgramFiles` and `ProgramFilesCommon` to the
 * 32 bit folders, and `ProgramFilesX64` is not supported for a 32 bit process
 * at all, which the empty result reports. Every other folder is the same one in
 * both views.
 *
 * @param[in] key Layer key of the folder.
 * @return The path of the folder in a 32 bit process, empty when it has none.
 */
std::wstring PathOfA32BitProcess(const std::wstring& key)
{
    if (key == L"#ProgramFiles#")
    {
        return appbox::test::GetKnownFolderPath(L"#ProgramFilesX86#", false);
    }
    if (key == L"#ProgramFilesCommon#")
    {
        return appbox::test::GetKnownFolderPath(L"#ProgramFilesCommonX86#", false);
    }
    if (key == L"#ProgramFilesX64#")
    {
        return {};
    }

    return appbox::test::GetKnownFolderPath(key, false);
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
    const std::wstring keys[] = { L"#ProgramFiles#",    L"#ProgramFilesCommon#",
                                  L"#ProgramFilesX86#", L"#ProgramFilesCommonX86#",
                                  L"#USERPROFILE#",     L"#Documents#",
                                  L"#Desktop#",         L"#AppData#",
                                  L"#LocalAppData#",    L"#LocalAppDataLow#",
                                  L"#Downloads#",       L"#Favorites#",
                                  L"#Music#",           L"#Pictures#",
                                  L"#StartMenu#",       L"#Programs#",
                                  L"#Startup#",         L"#ProgramData#",
                                  L"#Windows#",         L"#System32#",
                                  L"#SystemX86#",       L"#Fonts#" };

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
 * delimiters, so a folder which is added to the table later brings its variable
 * with it instead of having to be listed twice. Every variable carries the
 * folder of a 64 bit process and the folder of a 32 bit process, because the
 * operating system redirects some of them.
 */
TEST(Unit_ExpandKnownFolder, VariablesFollowTheKnownFolderTable)
{
    const std::wstring keys[] = { L"#ProgramFiles#",
                                  L"#ProgramFilesX64#",
                                  L"#ProgramFilesCommon#",
                                  L"#ProgramFilesCommonX64#",
                                  L"#ProgramFilesX86#",
                                  L"#ProgramFilesCommonX86#",
                                  L"#USERPROFILE#",
                                  L"#Documents#",
                                  L"#Desktop#",
                                  L"#AppData#",
                                  L"#LocalAppData#",
                                  L"#LocalAppDataLow#",
                                  L"#Downloads#",
                                  L"#Favorites#",
                                  L"#Music#",
                                  L"#Pictures#",
                                  L"#StartMenu#",
                                  L"#Programs#",
                                  L"#Startup#",
                                  L"#ProgramData#",
                                  L"#Windows#",
                                  L"#System32#",
                                  L"#SystemX86#",
                                  L"#Fonts#" };
    const char*        names[] = { "ProgramFiles",
                                   "ProgramFilesX64",
                                   "ProgramFilesCommon",
                                   "ProgramFilesCommonX64",
                                   "ProgramFilesX86",
                                   "ProgramFilesCommonX86",
                                   "USERPROFILE",
                                   "Documents",
                                   "Desktop",
                                   "AppData",
                                   "LocalAppData",
                                   "LocalAppDataLow",
                                   "Downloads",
                                   "Favorites",
                                   "Music",
                                   "Pictures",
                                   "StartMenu",
                                   "Programs",
                                   "Startup",
                                   "ProgramData",
                                   "Windows",
                                   "System32",
                                   "SystemX86",
                                   "Fonts" };

    const std::vector<appbox::KnownFolderVariable> variables = appbox::KnownFolderVariables();
    ASSERT_EQ(variables.size(), std::size(keys));
    ASSERT_EQ(variables.size(), static_cast<std::size_t>(24));

    for (std::size_t index = 0; index < std::size(keys); ++index)
    {
        EXPECT_EQ(variables[index].name, names[index]);

        /* The first view is the real folder of this machine. */
        EXPECT_EQ(variables[index].path, appbox::WideToUTF8(appbox::test::GetKnownFolderPath(keys[index], false)));

        /* The second view is the folder a 32 bit process sees. */
        EXPECT_EQ(variables[index].path_x86, appbox::WideToUTF8(PathOfA32BitProcess(keys[index])));
    }
}

/**
 * @brief A key which only exists for the variable expansion names no layer.
 *
 * `#ProgramFilesX64#` and `#ProgramFilesCommonX64#` name the 64 bit folder
 * unambiguously, which is what the capture references from a value that a 32
 * bit process may expand as well. No preset directory of the packer produces
 * them, so a layer directory named after such a key is rejected and a startup
 * file path which uses it is not expanded.
 */
TEST(Unit_ExpandKnownFolder, ExpansionOnlyKeysAreNoLayers)
{
    const std::wstring keys[] = { L"#ProgramFilesX64#", L"#ProgramFilesCommonX64#" };

    for (const auto& key : keys)
    {
        std::wstring folder;
        EXPECT_FALSE(appbox::SearchFolderID(key, folder));
        EXPECT_EQ(appbox::ExpandKnownFolder(key + L"\\MyApp\\app.exe"), key + L"\\MyApp\\app.exe");
    }
}

/**
 * @brief The capture writes only references whose meaning does not depend on
 *        the bitness of the process which expands them.
 *
 * `C:\Program Files` is the folder of a 64 bit process, while
 * `%APPBOX:ProgramFiles%` names `C:\Program Files (x86)` in a 32 bit one. The
 * capture therefore writes `%APPBOX:ProgramFilesX64%`, which names the 64 bit
 * folder in a 64 bit process and keeps its spelling in a 32 bit one; the two
 * redirected folders are not part of the table of the references at all.
 */
TEST(Unit_ExpandKnownFolder, ReferencesAreBitnessNeutral)
{
    const std::vector<appbox::KnownFolderReference> references = appbox::KnownFolderReferences();
    ASSERT_EQ(references.size(), static_cast<std::size_t>(22));

    std::vector<std::wstring> names;
    for (const auto& reference : references)
    {
        EXPECT_FALSE(reference.path.empty()) << reference.name;
        names.push_back(reference.name);

        if (reference.name == L"ProgramFilesX64")
        {
            EXPECT_EQ(reference.path, appbox::test::GetKnownFolderPath(L"#ProgramFilesX64#", false));
        }
        if (reference.name == L"SystemX86")
        {
            EXPECT_EQ(reference.path, appbox::test::GetKnownFolderPath(L"#SystemX86#", false));
        }
    }

    EXPECT_EQ(std::find(names.begin(), names.end(), L"ProgramFiles"), names.end());
    EXPECT_EQ(std::find(names.begin(), names.end(), L"ProgramFilesCommon"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), L"ProgramFilesX64"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), L"ProgramFilesCommonX64"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), L"SystemX86"), names.end());
}
