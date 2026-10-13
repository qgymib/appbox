#include <gtest/gtest.h>
#include <string>
#include "StartupFileType.hpp"

/**
 * @brief Every extension of the table names its own kind, so the launcher can
 *        tell how a startup file has to be started.
 */
TEST(Unit_StartupFileType, NamesTheKindOfEveryStartableExtension)
{
    EXPECT_EQ(appbox::StartupFileKindOf(L"app.exe"), appbox::StartupFileKind::Program);
    EXPECT_EQ(appbox::StartupFileKindOf(L"run.bat"), appbox::StartupFileKind::Batch);
    EXPECT_EQ(appbox::StartupFileKindOf(L"run.cmd"), appbox::StartupFileKind::CommandScript);
    EXPECT_EQ(appbox::StartupFileKindOf(L"deploy.ps1"), appbox::StartupFileKind::PowerShellScript);
}

/**
 * @brief A file name on Windows is compared ignoring the case, so the extension
 *        of a startup file has to be recognized in every spelling.
 */
TEST(Unit_StartupFileType, IgnoresTheCaseOfTheExtension)
{
    EXPECT_EQ(appbox::StartupFileKindOf(L"APP.EXE"), appbox::StartupFileKind::Program);
    EXPECT_EQ(appbox::StartupFileKindOf(L"Run.Bat"), appbox::StartupFileKind::Batch);
    EXPECT_EQ(appbox::StartupFileKindOf(L"RUN.CMD"), appbox::StartupFileKind::CommandScript);
    EXPECT_EQ(appbox::StartupFileKindOf(L"Deploy.Ps1"), appbox::StartupFileKind::PowerShellScript);
}

/**
 * @brief Only the files of the table are startable, everything else has to be
 *        refused so the dialog shows no file which cannot be started.
 */
TEST(Unit_StartupFileType, RefusesEveryOtherFile)
{
    EXPECT_EQ(appbox::StartupFileKindOf(L"notes.txt"), appbox::StartupFileKind::None);
    EXPECT_EQ(appbox::StartupFileKindOf(L"library.dll"), appbox::StartupFileKind::None);
    EXPECT_EQ(appbox::StartupFileKindOf(L"settings.ini"), appbox::StartupFileKind::None);
    EXPECT_EQ(appbox::StartupFileKindOf(L"archive.exe.bak"), appbox::StartupFileKind::None);
    EXPECT_EQ(appbox::StartupFileKindOf(L"noextension"), appbox::StartupFileKind::None);

    /* A trailing dot leaves the file without an extension. */
    EXPECT_EQ(appbox::StartupFileKindOf(L"trailing."), appbox::StartupFileKind::None);

    /* The name has to be longer than the extension itself. */
    EXPECT_EQ(appbox::StartupFileKindOf(L".exe"), appbox::StartupFileKind::None);
    EXPECT_EQ(appbox::StartupFileKindOf(L""), appbox::StartupFileKind::None);
}

/**
 * @brief The extension belongs to the file name, so a dot of a directory of the
 *        path must never be taken for the extension of the file.
 */
TEST(Unit_StartupFileType, OnlyTheFileNameCarriesTheExtension)
{
    EXPECT_EQ(appbox::StartupFileKindOf(L"C:\\Program Files\\MyApp\\app.exe"), appbox::StartupFileKind::Program);
    EXPECT_EQ(appbox::StartupFileKindOf(L"C:\\dir.d\\app.exe"), appbox::StartupFileKind::Program);
    EXPECT_EQ(appbox::StartupFileKindOf(L"bin\\tool.exe"), appbox::StartupFileKind::Program);

    /* A directory which is named like a startable file does not make its
       children startable. */
    EXPECT_EQ(appbox::StartupFileKindOf(L"C:\\dir.exe\\notes"), appbox::StartupFileKind::None);
    EXPECT_EQ(appbox::StartupFileKindOf(L"C:\\dir.bat\\notes"), appbox::StartupFileKind::None);

    /* The view paths of the packer carry a layer key which holds no dot. */
    EXPECT_EQ(appbox::StartupFileKindOf(L"#ProgramFiles#\\MyApp\\bin\\app.exe"), appbox::StartupFileKind::Program);
}

/**
 * @brief Both separators of Windows are accepted, because a caller may hand
 *        over a path which was spelled with forward slashes.
 */
TEST(Unit_StartupFileType, AcceptsBothSeparators)
{
    EXPECT_EQ(appbox::StartupFileKindOf(L"bin/tool.exe"), appbox::StartupFileKind::Program);
    EXPECT_EQ(appbox::StartupFileKindOf(L"dir.txt/notes"), appbox::StartupFileKind::None);
}

/**
 * @brief The predicate the four consumers share answers exactly the kinds of
 *        the table.
 */
TEST(Unit_StartupFileType, IsStartableFileFollowsTheKind)
{
    EXPECT_TRUE(appbox::IsStartableFile(L"app.exe"));
    EXPECT_TRUE(appbox::IsStartableFile(L"run.BAT"));
    EXPECT_TRUE(appbox::IsStartableFile(L"run.cmd"));
    EXPECT_TRUE(appbox::IsStartableFile(L"deploy.ps1"));

    EXPECT_FALSE(appbox::IsStartableFile(L"app.exe.txt"));
    EXPECT_FALSE(appbox::IsStartableFile(L"notes.txt"));
    EXPECT_FALSE(appbox::IsStartableFile(L""));
}

/**
 * @brief The readable extension list names every extension of the table, so a
 *        message which tells the user what can be chosen never omits one.
 */
TEST(Unit_StartupFileType, TheExtensionListNamesEveryExtension)
{
    const auto list = appbox::StartupFileExtensionList();

    EXPECT_EQ(list, ".exe, .bat, .cmd, .ps1");
    EXPECT_NE(list.find(".exe"), std::string::npos);
    EXPECT_NE(list.find(".bat"), std::string::npos);
    EXPECT_NE(list.find(".cmd"), std::string::npos);
    EXPECT_NE(list.find(".ps1"), std::string::npos);
}
