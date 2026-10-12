#include <gtest/gtest.h>
#include "src/core/StartupScreen.hpp"
#include "utils/RealFsFolder.hpp"
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Build a session which names an image.
 * @param[in] path Host path of the image.
 * @return The session.
 */
appbox::StartupScreen SessionWithImage(const std::wstring& path)
{
    appbox::StartupScreen screen;
    screen.image = path;
    return screen;
}

} // namespace

TEST(Unit_StartupScreen, DefaultSessionShowsTheNameAndTheProgressOnly)
{
    const appbox::StartupScreen screen;

    EXPECT_TRUE(screen.enabled);
    EXPECT_TRUE(screen.image.empty());
    EXPECT_TRUE(screen.IsEmpty());
    EXPECT_FALSE(appbox::HasStartupImage(screen));
}

TEST(Unit_StartupScreen, ASessionWithoutAnImageIsNotAPlainDefaultWhileItIsOff)
{
    appbox::StartupScreen screen;
    screen.enabled = false;

    /* The flag is a setting of the session, so it is stored like every other. */
    EXPECT_FALSE(screen.IsEmpty());
}

TEST(Unit_StartupScreen, SupportedFormatsAreTheDecodedOnes)
{
    const auto& extensions = appbox::StartupImageExtensions();

    const std::vector<std::wstring> expected = { L".png", L".jpg", L".jpeg", L".bmp" };
    EXPECT_EQ(extensions, expected);

    for (const auto& extension : extensions)
    {
        EXPECT_TRUE(appbox::IsSupportedStartupImage(L"D:\\images\\logo" + extension));
        EXPECT_TRUE(appbox::HasStartupImage(SessionWithImage(L"D:\\images\\logo" + extension)));
    }

    /* The case of the extension is the one of the file, which is not fixed. */
    EXPECT_TRUE(appbox::IsSupportedStartupImage(L"D:\\images\\LOGO.PNG"));
    EXPECT_TRUE(appbox::IsSupportedStartupImage(L"D:\\images\\logo.JpEg"));
}

TEST(Unit_StartupScreen, AFormatTheLauncherCannotDecodeIsNotAnImage)
{
    EXPECT_FALSE(appbox::IsSupportedStartupImage(L"D:\\images\\logo.gif"));
    EXPECT_FALSE(appbox::IsSupportedStartupImage(L"D:\\images\\logo.txt"));
    EXPECT_FALSE(appbox::IsSupportedStartupImage(L"D:\\images\\logo.pngx"));
    EXPECT_FALSE(appbox::IsSupportedStartupImage(L"D:\\images\\logo"));
    EXPECT_FALSE(appbox::IsSupportedStartupImage(L"D:\\images\\logo."));
    EXPECT_FALSE(appbox::IsSupportedStartupImage(L""));

    /* A session which names such a file describes a screen without an image. */
    EXPECT_FALSE(appbox::HasStartupImage(SessionWithImage(L"D:\\images\\logo.gif")));
}

TEST(Unit_StartupScreen, TheExtensionComesFromTheFileName)
{
    EXPECT_EQ(appbox::StartupImageExtension(L"D:\\images\\logo.png"), L".png");

    /* A dot of a directory is never the extension of the file. */
    EXPECT_EQ(appbox::StartupImageExtension(L"D:\\my.images\\logo"), L"");
    EXPECT_EQ(appbox::StartupImageExtension(L"D:\\my.images\\logo.PNG"), L".png");

    /* Only the last extension counts. */
    EXPECT_EQ(appbox::StartupImageExtension(L"D:\\images\\logo.backup.png"), L".png");

    EXPECT_EQ(appbox::StartupImageExtension(L"logo"), L"");
    EXPECT_EQ(appbox::StartupImageExtension(L""), L"");
}

TEST(Unit_StartupScreen, TheArchiveEntryIsTheNameOfTheLayout)
{
    /*
     * The launcher configuration carries this name, so it is part of the schema
     * of the archive and not a detail of the pack run.
     */
    EXPECT_EQ(appbox::StartupImageFileName(L"D:\\images\\logo.PNG"), L"splash.png");
    EXPECT_EQ(appbox::StartupImageEntryName(L"D:\\images\\logo.PNG"), "startup/splash.png");
    EXPECT_EQ(appbox::StartupImageEntryName(L"D:\\images\\photo.jpeg"), "startup/splash.jpeg");
    EXPECT_EQ(appbox::StartupImageEntryName(L"D:\\images\\photo.bmp"), "startup/splash.bmp");

    /* A file the launcher cannot decode produces no entry at all. */
    EXPECT_TRUE(appbox::StartupImageFileName(L"D:\\images\\logo.gif").empty());
    EXPECT_TRUE(appbox::StartupImageEntryName(L"D:\\images\\logo.gif").empty());
}

TEST(Unit_StartupScreen, ValidationAcceptsASessionWithoutAnImage)
{
    std::string error = "not cleared";

    EXPECT_TRUE(appbox::ValidateStartupScreen(appbox::StartupScreen(), error));
    EXPECT_TRUE(error.empty());

    /* A turned off screen is valid as well: it is packed as it is. */
    appbox::StartupScreen off;
    off.enabled = false;
    EXPECT_TRUE(appbox::ValidateStartupScreen(off, error));
    EXPECT_TRUE(error.empty());
}

TEST(Unit_StartupScreen, ValidationAcceptsAnImageOfASupportedFormat)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", L"appbox-startup-screen-valid");
    ASSERT_TRUE(folder.WriteFile(L"logo.png", "the content is read by the launcher, not by the packer"));

    const auto image = (folder.Get() / L"logo.png").wstring();

    std::string error = "not cleared";
    EXPECT_TRUE(appbox::ValidateStartupScreen(SessionWithImage(image), error));
    EXPECT_TRUE(error.empty());
}

TEST(Unit_StartupScreen, ValidationRejectsAMissingImage)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", L"appbox-startup-screen-missing");

    const auto image = (folder.Get() / L"missing.png").wstring();

    std::string error;
    EXPECT_FALSE(appbox::ValidateStartupScreen(SessionWithImage(image), error));
    EXPECT_NE(error.find("was not found"), std::string::npos);
}

TEST(Unit_StartupScreen, ValidationRejectsAFormatTheLauncherCannotDecode)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", L"appbox-startup-screen-format");
    ASSERT_TRUE(folder.WriteFile(L"logo.gif", "x"));

    const auto image = (folder.Get() / L"logo.gif").wstring();

    std::string error;
    EXPECT_FALSE(appbox::ValidateStartupScreen(SessionWithImage(image), error));
    EXPECT_NE(error.find("unsupported format"), std::string::npos);

    /* The reason names the file the session points at. */
    EXPECT_NE(error.find("logo.gif"), std::string::npos);
}

TEST(Unit_StartupScreen, ValidationRejectsADirectory)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", L"appbox-startup-screen-folder");
    ASSERT_TRUE(folder.WriteFile(L"logo.png\\placeholder.txt", "x"));

    const auto image = (folder.Get() / L"logo.png").wstring();

    /* A folder which carries the name of an image is not an image. */
    std::string error;
    EXPECT_FALSE(appbox::ValidateStartupScreen(SessionWithImage(image), error));
    EXPECT_FALSE(error.empty());
}
