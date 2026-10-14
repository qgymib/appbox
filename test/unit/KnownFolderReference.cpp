#include <gtest/gtest.h>
#include "src/core/KnownFolderReference.hpp"
#include "src/core/PresetDirectory.hpp"
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
 * @brief Get the real path of a preset directory.
 * @param[in] preset_id Identifier of the preset directory.
 * @return The path, empty when the preset directory is not offered.
 */
std::wstring PresetPath(const std::string& preset_id)
{
    appbox::PresetDirectory preset;
    if (!appbox::FindPresetDirectory(preset_id, preset))
    {
        return {};
    }

    return preset.real_path;
}

/**
 * @brief Rewrite the paths of a text with the preset directories of this machine.
 * @param[in] text Text to rewrite.
 * @param[out] changed Whether a path was replaced.
 * @return The rewritten text.
 */
std::wstring Rewrite(const std::wstring& text, bool& changed)
{
    return appbox::RewriteKnownFolderReferences(text, appbox::PresetDirectories(), changed);
}

} // namespace

TEST(Unit_KnownFolderReference, TheNameOfAReferenceDropsTheDelimiters)
{
    EXPECT_EQ(appbox::KnownFolderReferenceName(L"#ProgramFiles#"), L"ProgramFiles");
    EXPECT_EQ(appbox::KnownFolderReferenceName(L"#USERPROFILE#"), L"USERPROFILE");
    EXPECT_EQ(appbox::KnownFolderReferenceName(L"ProgramFiles"), L"ProgramFiles");
    EXPECT_EQ(appbox::KnownFolderReferenceName(L"#"), L"");
    EXPECT_EQ(appbox::KnownFolderReferenceName(L""), L"");
}

TEST(Unit_KnownFolderReference, ReplacesThePathOfAPresetDirectory)
{
    const auto path = PresetPath("program_files");
    ASSERT_FALSE(path.empty());

    bool       changed = false;
    const auto text = Rewrite(path + L"\\Foo\\Bar.exe", changed);

    EXPECT_TRUE(changed);
    EXPECT_EQ(text, L"%APPBOX:ProgramFiles%\\Foo\\Bar.exe");
}

TEST(Unit_KnownFolderReference, KeepsTheSeparatorBehindThePath)
{
    const auto path = PresetPath("program_files");
    ASSERT_FALSE(path.empty());

    bool changed = false;
    EXPECT_EQ(Rewrite(path, changed), L"%APPBOX:ProgramFiles%");
    EXPECT_TRUE(changed);

    changed = false;
    EXPECT_EQ(Rewrite(path + L"\\", changed), L"%APPBOX:ProgramFiles%\\");
    EXPECT_TRUE(changed);
}

TEST(Unit_KnownFolderReference, IgnoresTheCaseOfThePath)
{
    const auto path = PresetPath("program_files");
    ASSERT_FALSE(path.empty());

    std::wstring upper = path;
    for (auto& character : upper)
    {
        character = static_cast<wchar_t>(std::towupper(static_cast<std::wint_t>(character)));
    }

    bool changed = false;
    EXPECT_EQ(Rewrite(upper + L"\\Foo", changed), L"%APPBOX:ProgramFiles%\\Foo");
    EXPECT_TRUE(changed);
}

TEST(Unit_KnownFolderReference, RequiresABoundaryAroundThePath)
{
    const auto path = PresetPath("program_files");
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
    EXPECT_EQ(Rewrite(L"\"" + path + L"\\Foo\"", changed), L"\"%APPBOX:ProgramFiles%\\Foo\"");
    EXPECT_TRUE(changed);

    changed = false;
    EXPECT_EQ(Rewrite(L"a;" + path + L";b", changed), L"a;%APPBOX:ProgramFiles%;b");
    EXPECT_TRUE(changed);
}

TEST(Unit_KnownFolderReference, PrefersTheLongestPath)
{
    /*
     * `Common` hangs below `Program Files`, so the longer path names the nested
     * preset directory and not the folder of its parent.
     */
    const auto common = PresetPath("program_files_common");
    ASSERT_FALSE(common.empty());

    bool changed = false;
    EXPECT_EQ(Rewrite(common + L"\\Foo", changed), L"%APPBOX:ProgramFilesCommon%\\Foo");
    EXPECT_TRUE(changed);
}

TEST(Unit_KnownFolderReference, ReplacesEveryPathOfAText)
{
    const auto files = PresetPath("program_files");
    const auto profile = PresetPath("user_profile");
    ASSERT_FALSE(files.empty());
    ASSERT_FALSE(profile.empty());

    bool changed = false;
    EXPECT_EQ(Rewrite(files + L"\\a;" + profile + L"\\b", changed),
              L"%APPBOX:ProgramFiles%\\a;%APPBOX:USERPROFILE%\\b");
    EXPECT_TRUE(changed);
}

TEST(Unit_KnownFolderReference, LeavesAnUnknownPathAlone)
{
    bool changed = false;
    EXPECT_EQ(Rewrite(L"C:\\AppBoxNoSuchFolder\\Foo", changed), L"C:\\AppBoxNoSuchFolder\\Foo");
    EXPECT_FALSE(changed);

    /* A reference of the archive is not a path of a preset directory. */
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
    const auto path = PresetPath("program_files");
    ASSERT_FALSE(path.empty());

    const auto data = appbox::RegistryStringData(path + L"\\Foo\\Bar.exe");

    bool       changed = false;
    const auto rewritten = appbox::RewriteRegistryValueReferences(REG_SZ, data, appbox::PresetDirectories(), changed);

    EXPECT_TRUE(changed);

    const std::wstring expected = L"%APPBOX:ProgramFiles%\\Foo\\Bar.exe";
    EXPECT_EQ(appbox::RegistryStringValue(rewritten), expected);

    /* The rewritten value carries exactly one terminator, like the original one. */
    EXPECT_EQ(rewritten.size(), (expected.size() + 1) * sizeof(wchar_t));
}

TEST(Unit_KnownFolderReference, RewritesEveryItemOfAMultiStringValue)
{
    const auto path = PresetPath("program_files");
    ASSERT_FALSE(path.empty());

    const auto data = appbox::RegistryMultiStringData({ path + L"\\a", L"plain", path + L"\\b" });

    bool       changed = false;
    const auto rewritten =
        appbox::RewriteRegistryValueReferences(REG_MULTI_SZ, data, appbox::PresetDirectories(), changed);

    EXPECT_TRUE(changed);

    const auto parts = appbox::RegistryMultiStringValue(rewritten);
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0], L"%APPBOX:ProgramFiles%\\a");
    EXPECT_EQ(parts[1], L"plain");
    EXPECT_EQ(parts[2], L"%APPBOX:ProgramFiles%\\b");
}

TEST(Unit_KnownFolderReference, LeavesEveryOtherTypeAlone)
{
    const auto path = PresetPath("program_files");
    ASSERT_FALSE(path.empty());

    std::vector<std::uint8_t> data(path.size() * sizeof(wchar_t));
    std::memcpy(data.data(), path.data(), data.size());

    bool changed = false;
    EXPECT_EQ(appbox::RewriteRegistryValueReferences(REG_DWORD, data, appbox::PresetDirectories(), changed), data);
    EXPECT_FALSE(changed);

    changed = false;
    EXPECT_EQ(appbox::RewriteRegistryValueReferences(REG_BINARY, data, appbox::PresetDirectories(), changed), data);
    EXPECT_FALSE(changed);
}

TEST(Unit_KnownFolderReference, LeavesAValueWithoutAPathAlone)
{
    const auto data = appbox::RegistryStringData(L"plain");

    bool       changed = false;
    const auto rewritten = appbox::RewriteRegistryValueReferences(REG_SZ, data, appbox::PresetDirectories(), changed);

    EXPECT_FALSE(changed);
    EXPECT_EQ(rewritten, data);
}
