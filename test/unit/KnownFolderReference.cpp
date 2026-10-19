#include <gtest/gtest.h>
#include "src/core/KnownFolderReference.hpp"
#include "src/core/RegistryModel.hpp"
#include "WString.hpp"
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Get the path of a reference of the table of this machine.
 * @param[in] name Name of the reference without the `%APPBOX:` prefix.
 * @return The path, empty when the table does not name the reference.
 */
std::wstring ReferencePath(const std::wstring& name)
{
    for (const auto& reference : appbox::KnownFolderReferences())
    {
        if (reference.name == name)
        {
            return reference.path;
        }
    }

    return {};
}

/**
 * @brief Rewrite the paths of a text with the references of this machine.
 * @param[in] text Text to rewrite.
 * @param[out] changed Whether a path was replaced.
 * @return The rewritten text.
 */
std::wstring Rewrite(const std::wstring& text, bool& changed)
{
    return appbox::RewriteKnownFolderReferences(text, appbox::KnownFolderReferences(), changed);
}

} // namespace

TEST(Unit_KnownFolderReference, ReplacesThePathOfAKnownFolder)
{
    const auto path = ReferencePath(L"ProgramFilesX64");
    ASSERT_FALSE(path.empty());

    bool       changed = false;
    const auto text = Rewrite(path + L"\\Foo\\Bar.exe", changed);

    EXPECT_TRUE(changed);
    EXPECT_EQ(text, L"%APPBOX:ProgramFilesX64%\\Foo\\Bar.exe");
}

TEST(Unit_KnownFolderReference, KeepsTheSeparatorBehindThePath)
{
    const auto path = ReferencePath(L"ProgramFilesX64");
    ASSERT_FALSE(path.empty());

    bool changed = false;
    EXPECT_EQ(Rewrite(path, changed), L"%APPBOX:ProgramFilesX64%");
    EXPECT_TRUE(changed);

    changed = false;
    EXPECT_EQ(Rewrite(path + L"\\", changed), L"%APPBOX:ProgramFilesX64%\\");
    EXPECT_TRUE(changed);
}

TEST(Unit_KnownFolderReference, IgnoresTheCaseOfThePath)
{
    const auto path = ReferencePath(L"ProgramFilesX64");
    ASSERT_FALSE(path.empty());

    std::wstring upper = path;
    for (auto& character : upper)
    {
        character = static_cast<wchar_t>(std::towupper(static_cast<std::wint_t>(character)));
    }

    bool changed = false;
    EXPECT_EQ(Rewrite(upper + L"\\Foo", changed), L"%APPBOX:ProgramFilesX64%\\Foo");
    EXPECT_TRUE(changed);
}

TEST(Unit_KnownFolderReference, RequiresABoundaryAroundThePath)
{
    const auto path = ReferencePath(L"ProgramFilesX64");
    ASSERT_FALSE(path.empty());

    /* A path which is glued to another word is not the folder. */
    bool changed = false;
    EXPECT_EQ(Rewrite(L"x" + path + L"\\Foo", changed), L"x" + path + L"\\Foo");
    EXPECT_FALSE(changed);

    /* A longer name which starts with the path names another folder. */
    changed = false;
    EXPECT_EQ(Rewrite(path + L"X", changed), path + L"X");
    EXPECT_FALSE(changed);

    changed = false;
    EXPECT_EQ(Rewrite(path + L".txt", changed), path + L".txt");
    EXPECT_FALSE(changed);

    /* Behind a quote or a separator the path starts. */
    changed = false;
    EXPECT_EQ(Rewrite(L"\"" + path + L"\\Foo\"", changed), L"\"%APPBOX:ProgramFilesX64%\\Foo\"");
    EXPECT_TRUE(changed);

    changed = false;
    EXPECT_EQ(Rewrite(L"a;" + path + L";b", changed), L"a;%APPBOX:ProgramFilesX64%;b");
    EXPECT_TRUE(changed);
}

TEST(Unit_KnownFolderReference, PrefersTheLongestPath)
{
    /*
     * `Common Files` lives inside `Program Files`, so the longer path names the
     * nested folder and not the folder of its parent.
     */
    const auto common = ReferencePath(L"ProgramFilesCommonX64");
    ASSERT_FALSE(common.empty());

    bool changed = false;
    EXPECT_EQ(Rewrite(common + L"\\Foo", changed), L"%APPBOX:ProgramFilesCommonX64%\\Foo");
    EXPECT_TRUE(changed);
}

/**
 * @brief The 32 bit folders of the program files share their name with the 64
 *        bit ones, so the longest path tells them apart.
 *
 * `C:\Program Files (x86)` starts with the path of `C:\Program Files`, but the
 * longer path of the 32 bit folder wins, which keeps the two folders apart. The
 * capture writes the 64 bit folder as `ProgramFilesX64` and the 32 bit one as
 * `ProgramFilesX86`: the first reference names the 64 bit folder in a 64 bit
 * process and keeps its spelling in a 32 bit one, while the second names the
 * same folder in both.
 */
TEST(Unit_KnownFolderReference, DistinguishesThe32BitProgramFolders)
{
    const auto programs = ReferencePath(L"ProgramFilesX64");
    const auto programs_x86 = ReferencePath(L"ProgramFilesX86");
    const auto common_x86 = ReferencePath(L"ProgramFilesCommonX86");
    ASSERT_FALSE(programs.empty());
    ASSERT_FALSE(programs_x86.empty());
    ASSERT_FALSE(common_x86.empty());

    /* The 32 bit folder names its own reference, not the one it shares a prefix
     * with. */
    bool changed = false;
    EXPECT_EQ(Rewrite(programs_x86 + L"\\Foo", changed), L"%APPBOX:ProgramFilesX86%\\Foo");
    EXPECT_TRUE(changed);

    /* The `Common Files` folder of the 32 bit folder names its own reference. */
    changed = false;
    EXPECT_EQ(Rewrite(common_x86 + L"\\Foo", changed), L"%APPBOX:ProgramFilesCommonX86%\\Foo");
    EXPECT_TRUE(changed);

    /* The folder of the 64 bit process keeps its own reference. */
    changed = false;
    EXPECT_EQ(Rewrite(programs + L"\\Foo", changed), L"%APPBOX:ProgramFilesX64%\\Foo");
    EXPECT_TRUE(changed);
}

/**
 * @brief The two system folders of a 64 bit machine are told apart.
 *
 * `C:\Windows\SysWOW64` lives inside `C:\Windows`, so the longest path decides
 * which reference is written: the 32 bit system folder is `SystemX86` and the
 * 64 bit one is `System32`, exactly like the expansion of the sandbox rebuilds
 * them.
 */
TEST(Unit_KnownFolderReference, DistinguishesTheSystemFolders)
{
    const auto system32 = ReferencePath(L"System32");
    const auto system_x86 = ReferencePath(L"SystemX86");
    ASSERT_FALSE(system32.empty());
    ASSERT_FALSE(system_x86.empty());

    bool changed = false;
    EXPECT_EQ(Rewrite(system32 + L"\\Foo", changed), L"%APPBOX:System32%\\Foo");
    EXPECT_TRUE(changed);

    changed = false;
    EXPECT_EQ(Rewrite(system_x86 + L"\\Foo", changed), L"%APPBOX:SystemX86%\\Foo");
    EXPECT_TRUE(changed);
}

TEST(Unit_KnownFolderReference, ReplacesEveryPathOfAText)
{
    const auto files = ReferencePath(L"ProgramFilesX64");
    const auto profile = ReferencePath(L"USERPROFILE");
    ASSERT_FALSE(files.empty());
    ASSERT_FALSE(profile.empty());

    bool changed = false;
    EXPECT_EQ(Rewrite(files + L"\\a;" + profile + L"\\b", changed),
              L"%APPBOX:ProgramFilesX64%\\a;%APPBOX:USERPROFILE%\\b");
    EXPECT_TRUE(changed);
}

TEST(Unit_KnownFolderReference, LeavesAnUnknownPathAlone)
{
    bool changed = false;
    EXPECT_EQ(Rewrite(L"C:\\AppBoxNoSuchFolder\\Foo", changed), L"C:\\AppBoxNoSuchFolder\\Foo");
    EXPECT_FALSE(changed);

    /* A reference of the archive is not a path of a known folder. */
    changed = false;
    EXPECT_EQ(Rewrite(L"%APPBOX:ProgramFiles%\\Foo", changed), L"%APPBOX:ProgramFiles%\\Foo");
    EXPECT_FALSE(changed);

    /* An empty text is returned as it is. */
    changed = false;
    EXPECT_EQ(Rewrite(std::wstring(), changed), std::wstring());
    EXPECT_FALSE(changed);
}

TEST(Unit_KnownFolderReference, RewritesAStringValueAndKeepsItsTerminator)
{
    const auto path = ReferencePath(L"ProgramFilesX64");
    ASSERT_FALSE(path.empty());

    const auto data = appbox::RegistryStringData(path + L"\\Foo\\Bar.exe");

    bool       changed = false;
    const auto rewritten =
        appbox::RewriteRegistryValueReferences(REG_SZ, data, appbox::KnownFolderReferences(), changed);

    EXPECT_TRUE(changed);

    const std::wstring expected = L"%APPBOX:ProgramFilesX64%\\Foo\\Bar.exe";
    EXPECT_EQ(appbox::RegistryStringValue(rewritten), expected);

    /* The rewritten value carries exactly one terminator, like the original one. */
    EXPECT_EQ(rewritten.size(), (expected.size() + 1) * sizeof(wchar_t));
}

TEST(Unit_KnownFolderReference, RewritesEveryItemOfAMultiStringValue)
{
    const auto path = ReferencePath(L"ProgramFilesX64");
    ASSERT_FALSE(path.empty());

    const auto data = appbox::RegistryMultiStringData({ path + L"\\a", L"plain", path + L"\\b" });

    bool       changed = false;
    const auto rewritten =
        appbox::RewriteRegistryValueReferences(REG_MULTI_SZ, data, appbox::KnownFolderReferences(), changed);

    EXPECT_TRUE(changed);

    const auto parts = appbox::RegistryMultiStringValue(rewritten);
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0], L"%APPBOX:ProgramFilesX64%\\a");
    EXPECT_EQ(parts[1], L"plain");
    EXPECT_EQ(parts[2], L"%APPBOX:ProgramFilesX64%\\b");
}

TEST(Unit_KnownFolderReference, LeavesEveryOtherTypeAlone)
{
    const auto path = ReferencePath(L"ProgramFilesX64");
    ASSERT_FALSE(path.empty());

    std::vector<std::uint8_t> data(path.size() * sizeof(wchar_t));
    std::memcpy(data.data(), path.data(), data.size());

    bool changed = false;
    EXPECT_EQ(appbox::RewriteRegistryValueReferences(REG_DWORD, data, appbox::KnownFolderReferences(), changed), data);
    EXPECT_FALSE(changed);

    changed = false;
    EXPECT_EQ(appbox::RewriteRegistryValueReferences(REG_BINARY, data, appbox::KnownFolderReferences(), changed), data);
    EXPECT_FALSE(changed);
}

TEST(Unit_KnownFolderReference, LeavesAValueWithoutAPathAlone)
{
    const auto data = appbox::RegistryStringData(L"plain");

    bool       changed = false;
    const auto rewritten =
        appbox::RewriteRegistryValueReferences(REG_SZ, data, appbox::KnownFolderReferences(), changed);

    EXPECT_FALSE(changed);
    EXPECT_EQ(rewritten, data);
}
