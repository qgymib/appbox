#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/LauncherPath.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/TestBitmap.hpp"
#include "utils/WriteFileFull.hpp"
#include "launcher/Config.hpp"
#include "src/core/LauncherResources.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

typedef appbox::test::CommonFixture E2E_Launcher_StartupScreen;
using namespace appbox::test;

namespace
{

/** Name of the directory the run of a case writes its log files into. */
constexpr const wchar_t* kLogDirName = appbox::test::kCaseLogDirName;

/**
 * @brief Build the launcher configuration of a case.
 *
 * The configuration carries the filesystem of the sandbox the launcher mounts:
 * an empty upper layer and a lower layer below `filesystem`, which is the
 * layout a packaged archive uses.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @return The configuration without any startup file.
 */
appbox::LauncherConfig BuildConfig(const std::filesystem::path& root)
{
    /* clang-format off */
    auto tree = FsRoot(root, {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"filesystem\\#USERPROFILE#", {}) })
    });
    /* clang-format on */

    return tree.Build();
}

/**
 * @brief Append one startup file to a configuration.
 *
 * The arguments of the file carry its marker, which the probe process of the
 * case reports back: the marker tells that the launcher started the file.
 *
 * @param[in,out] launcher_config Configuration to extend.
 * @param[in] trigger Trigger and marker of the startup file.
 */
void AddStartup(appbox::LauncherConfig& launcher_config, const std::string& trigger)
{
    appbox::LauncherStartup startup;
    startup.trigger = trigger;
    startup.auto_start = true;
    startup.arguments = { "--startup_marker", trigger };
    launcher_config.startups.push_back(std::move(startup));
}

/**
 * @brief Build the bytes of a startup image of a case.
 *
 * The image is a bitmap of the size the case names, which the screen reports in
 * its log line: the size tells that the bytes were decoded.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @param[out] bytes The bytes of the image, empty on failure.
 * @return true when the image was written and read back.
 */
bool BitmapBytes(const std::filesystem::path& root, std::string& bytes)
{
    bytes.clear();

    const auto image = root / L"logo.bmp";
    if (!appbox::test::WriteTestBitmap(image, 8, 4, 10, 20, 30))
    {
        return false;
    }

    return appbox::test::ReadFileFull(image.wstring(), bytes) == 0;
}

/**
 * @brief Build a launcher whose image carries the resources of a session.
 *
 * The payload is the launcher under test with the configuration of the case and
 * the bytes of the startup image embedded into it, which is what a pack run
 * produces (see `src/core/LauncherResources.cpp`). The program is written into
 * the working directory of the case, so the run of the case starts it beside
 * its own state.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @param[in] launcher_config Configuration of the case.
 * @param[in] image_bytes Bytes of the startup image, empty for a session
 *                        without one.
 * @param[out] error Description of the failure, empty on success.
 * @return Path of the patched launcher, empty on failure.
 */
std::wstring MakeLauncherWithImage(const std::filesystem::path& root, const appbox::LauncherConfig& launcher_config,
                                   const std::string& image_bytes, std::string& error)
{
    error.clear();

    std::string payload;
    if (appbox::test::ReadFileFull(appbox::test::LauncherPath(), payload) != 0)
    {
        error = "the launcher payload cannot be read";
        return {};
    }

    const auto patched = appbox::EmbedLauncherResources(
        payload.data(), payload.size(), appbox::BuildLauncherConfigJson(launcher_config), image_bytes, error);
    if (patched.empty())
    {
        return {};
    }

    const auto target = root / L"launcher-with-image.exe";
    if (appbox::test::WriteFileFull(target.wstring(), std::string(patched.begin(), patched.end())) != 0)
    {
        error = "the patched launcher cannot be written";
        return {};
    }

    return target.wstring();
}

/**
 * @brief Read the log files the run of a case wrote.
 *
 * The launcher writes a log file of its own into the log directory of the case
 * and every process of its sandbox writes one as well, so the directory is read
 * as a whole: a case looks for a message of the run and not for the file which
 * carries it.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @return The content of the log files, empty when the run wrote none.
 */
std::string ReadLog(const std::filesystem::path& root)
{
    std::string     text;
    std::error_code ec;

    for (const auto& entry : std::filesystem::directory_iterator(root / kLogDirName, ec))
    {
        if (!entry.is_regular_file(ec) || entry.path().extension() != L".log")
        {
            continue;
        }

        std::ifstream stream(entry.path(), std::ios::binary);
        if (!stream.is_open())
        {
            continue;
        }

        text.append(std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()));
    }

    return text;
}

} // namespace

/**
 * Condition:
 * 1. The launcher payload carries the startup image resource of an 8x4 bitmap.
 * 2. The configuration holds one startup file whose startup screen is on.
 *
 * Expected:
 * 1. The startup file is started as it is without a screen, and the launcher
 *    exits with its exit code (zero).
 * 2. The screen was shown while the run prepared the sandbox and it was closed
 *    once the process of the run was started.
 * 3. The screen decoded the image resource, which the log reports with the size
 *    of that image.
 */
TEST_F(E2E_Launcher_StartupScreen, TheScreenIsShownWhileTheApplicationStarts)
{
    auto launcher_config = BuildConfig(GetCWD());
    launcher_config.startup.screen = true;
    AddStartup(launcher_config, "app");

    std::string bytes;
    ASSERT_TRUE(BitmapBytes(GetCWD(), bytes));

    std::string error;
    const auto  launcher = MakeLauncherWithImage(GetCWD(), launcher_config, bytes, error);
    ASSERT_FALSE(launcher.empty()) << error;

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "", true, LogRun{}, false, launcher);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    const auto log = ReadLog(GetCWD());
    EXPECT_NE(log.find("the startup screen is shown"), std::string::npos) << log;
    EXPECT_NE(log.find(", image 8x4)"), std::string::npos) << log;
    EXPECT_NE(log.find("the startup screen is closed"), std::string::npos) << log;
}

/**
 * Condition:
 * 1. The launcher payload carries no startup image resource at all, which is
 *    the payload of a session without an image.
 * 2. The configuration holds one startup file whose startup screen is on.
 *
 * Expected:
 * 1. The run is not refused: the startup file is started and the launcher
 *    exits with its exit code (zero).
 * 2. The screen is shown without an image, which the log reports with an image
 *    size of zero, so the name of the application and the progress stay
 *    visible.
 */
TEST_F(E2E_Launcher_StartupScreen, AScreenWithoutAnImageResourceKeepsTheRunAlive)
{
    auto launcher_config = BuildConfig(GetCWD());
    launcher_config.startup.screen = true;
    AddStartup(launcher_config, "app");

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "", true);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    const auto log = ReadLog(GetCWD());
    EXPECT_NE(log.find("the startup screen is shown"), std::string::npos) << log;
    EXPECT_NE(log.find(", image 0x0)"), std::string::npos) << log;
    EXPECT_NE(log.find("the startup screen is closed"), std::string::npos) << log;
}

/**
 * Condition:
 * 1. The launcher payload carries a startup image resource which is not an
 *    image at all.
 * 2. The configuration holds one startup file whose startup screen is on.
 *
 * Expected:
 * 1. The run is not refused: the startup file is started and the launcher
 *    exits with its exit code (zero).
 * 2. The screen is shown without an image, which the log reports.
 */
TEST_F(E2E_Launcher_StartupScreen, AnImageWhichCannotBeDecodedKeepsTheRunAlive)
{
    auto launcher_config = BuildConfig(GetCWD());
    launcher_config.startup.screen = true;
    AddStartup(launcher_config, "app");

    std::string error;
    const auto  launcher = MakeLauncherWithImage(GetCWD(), launcher_config, "this is not an image at all", error);
    ASSERT_FALSE(launcher.empty()) << error;

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "", true, LogRun{}, false, launcher);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    const auto log = ReadLog(GetCWD());
    EXPECT_NE(log.find("the startup screen is shown"), std::string::npos) << log;
    EXPECT_NE(log.find("without its image"), std::string::npos) << log;
}

/**
 * Condition:
 * 1. The launcher payload carries the startup image resource of a case.
 * 2. The configuration holds one startup file and turns the startup screen off.
 *
 * Expected:
 * 1. The startup file is started and the launcher exits with its exit code
 *    (zero).
 * 2. No screen is created at all, which the log proves: neither the message
 *    which reports a shown screen nor the one which reports a closed screen is
 *    written.
 */
TEST_F(E2E_Launcher_StartupScreen, ASessionWithoutAScreenCreatesNoWindow)
{
    auto launcher_config = BuildConfig(GetCWD());
    launcher_config.startup.screen = false;
    AddStartup(launcher_config, "app");

    std::string bytes;
    ASSERT_TRUE(BitmapBytes(GetCWD(), bytes));

    std::string error;
    const auto  launcher = MakeLauncherWithImage(GetCWD(), launcher_config, bytes, error);
    ASSERT_FALSE(launcher.empty()) << error;

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "", false, LogRun{}, false, launcher);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    const auto log = ReadLog(GetCWD());
    EXPECT_EQ(log.find("the startup screen is shown"), std::string::npos) << log;
    EXPECT_EQ(log.find("the startup screen is closed"), std::string::npos) << log;
}
