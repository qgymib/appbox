#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/TestBitmap.hpp"
#include "launcher/Config.hpp"
#include "src/core/StartupScreen.hpp"
#include "SandboxLayout.hpp"
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

/** Name of the log file the launcher of a case writes. */
constexpr const wchar_t* kLogFileName = L"log.txt";

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
 * @param[in,out] config Configuration to extend.
 * @param[in] trigger Trigger and marker of the startup file.
 */
void AddStartup(appbox::LauncherConfig& config, const std::string& trigger)
{
    appbox::LauncherStartup startup;
    startup.trigger = trigger;
    startup.auto_start = true;
    startup.arguments = { "--startup_marker", trigger };
    config.startups.push_back(std::move(startup));
}

/**
 * @brief Get the folder which carries the startup image of a case.
 * @param[in] root Root directory of the filesystem of the case.
 * @return The path of the startup folder below `app`.
 */
std::filesystem::path StartupFolder(const std::filesystem::path& root)
{
    return root / appbox::layout::kAppDirNameW / appbox::layout::kStartupDirNameW;
}

/**
 * @brief Write the startup image of a case.
 *
 * The image is stored under the name the archive layout gives it, so the
 * configuration of the case names the entry the launcher resolves.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @param[in] width Width of the image in pixels.
 * @param[in] height Height of the image in pixels.
 * @param[out] entry Entry name the configuration has to carry.
 * @return true when the image was written.
 */
bool WriteStartupImage(const std::filesystem::path& root, int width, int height, std::string& entry)
{
    const auto folder = StartupFolder(root);

    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec)
    {
        return false;
    }

    const auto image = folder / (std::wstring(appbox::layout::kStartupImageBaseNameW) + L".bmp");
    if (!appbox::test::WriteTestBitmap(image, width, height, 10, 20, 30))
    {
        return false;
    }

    entry = appbox::StartupImageEntryName(image.wstring());
    return !entry.empty();
}

/**
 * @brief Read the log the launcher of a case wrote.
 * @param[in] root Root directory of the filesystem of the case.
 * @return The content of the log, empty when it cannot be read.
 */
std::string ReadLog(const std::filesystem::path& root)
{
    std::ifstream stream(root / kLogFileName, std::ios::binary);
    if (!stream.is_open())
    {
        return {};
    }

    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

} // namespace

/**
 * Condition:
 * 1. The configuration holds one startup file and a startup screen which
 *    presents an image of `app/startup/splash.bmp`.
 * 2. The launcher runs with the startup screen turned on.
 *
 * Expected:
 * 1. The startup file is started as it is without a screen, and the launcher
 *    exits with its exit code (zero).
 * 2. The screen was shown while the run prepared the sandbox and it was closed
 *    once the process of the run was started.
 * 3. The screen decoded the image of the session, which the log reports with
 *    the size of that image.
 */
TEST_F(E2E_Launcher_StartupScreen, TheScreenIsShownWhileTheApplicationStarts)
{
    auto        config = BuildConfig(GetCWD());
    std::string entry;
    ASSERT_TRUE(WriteStartupImage(GetCWD(), 8, 4, entry)) << entry;
    config.startup.image = entry;
    AddStartup(config, "app");

    const auto run = ProbeStartupRun(GetCWDString(), config, "", true);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    const auto log = ReadLog(GetCWD());
    EXPECT_NE(log.find("the startup screen is shown"), std::string::npos) << log;
    EXPECT_NE(log.find(", image 8x4)"), std::string::npos) << log;
    EXPECT_NE(log.find("the startup screen is closed"), std::string::npos) << log;
}

/**
 * Condition:
 * 1. The configuration names a startup image which does not exist.
 * 2. The launcher runs with the startup screen turned on.
 *
 * Expected:
 * 1. The run is not refused: the startup file is started and the launcher
 *    exits with its exit code (zero).
 * 2. The screen is shown without an image, which the log reports, so the name
 *    of the application and the progress stay visible.
 */
TEST_F(E2E_Launcher_StartupScreen, AScreenWithoutItsImageKeepsTheRunAlive)
{
    auto        config = BuildConfig(GetCWD());
    std::string entry;
    ASSERT_TRUE(WriteStartupImage(GetCWD(), 8, 4, entry));
    config.startup.image = entry;

    /* The image of the session is dropped, so the file is not there anymore. */
    std::error_code ec;
    std::filesystem::remove_all(StartupFolder(GetCWD()), ec);

    AddStartup(config, "app");

    const auto run = ProbeStartupRun(GetCWDString(), config, "", true);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    const auto log = ReadLog(GetCWD());
    EXPECT_NE(log.find("the startup screen is shown"), std::string::npos) << log;
    EXPECT_NE(log.find("without its image"), std::string::npos) << log;
    EXPECT_NE(log.find("the startup screen is closed"), std::string::npos) << log;
}

/**
 * Condition:
 * 1. The configuration names a startup image which is not an image at all.
 * 2. The launcher runs with the startup screen turned on.
 *
 * Expected:
 * 1. The run is not refused: the startup file is started and the launcher
 *    exits with its exit code (zero).
 * 2. The screen is shown without an image, which the log reports.
 */
TEST_F(E2E_Launcher_StartupScreen, AnImageWhichCannotBeDecodedKeepsTheRunAlive)
{
    auto        config = BuildConfig(GetCWD());
    std::string entry;
    ASSERT_TRUE(WriteStartupImage(GetCWD(), 8, 4, entry));
    config.startup.image = entry;

    /* The file keeps the name of an image, but it holds no image. */
    const auto image = StartupFolder(GetCWD()) / (std::wstring(appbox::layout::kStartupImageBaseNameW) + L".bmp");
    {
        std::ofstream stream(image, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(stream.is_open());
        stream << "this is not an image at all";
    }

    AddStartup(config, "app");

    const auto run = ProbeStartupRun(GetCWDString(), config, "", true);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    const auto log = ReadLog(GetCWD());
    EXPECT_NE(log.find("the startup screen is shown"), std::string::npos) << log;
    EXPECT_NE(log.find("without its image"), std::string::npos) << log;
}

/**
 * Condition:
 * 1. The configuration holds one startup file and a startup image.
 * 2. The session turns the startup screen off.
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
    auto        config = BuildConfig(GetCWD());
    std::string entry;
    ASSERT_TRUE(WriteStartupImage(GetCWD(), 8, 4, entry));
    config.startup.image = entry;
    config.startup.screen = false;
    AddStartup(config, "app");

    const auto run = ProbeStartupRun(GetCWDString(), config, "", false);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    const auto log = ReadLog(GetCWD());
    EXPECT_EQ(log.find("the startup screen is shown"), std::string::npos) << log;
    EXPECT_EQ(log.find("the startup screen is closed"), std::string::npos) << log;
}
