#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/LauncherPath.hpp"
#include "utils/ProbeCall.hpp"
#include "launcher/Config.hpp"
#include "WString.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

typedef appbox::test::CommonFixture E2E_Launcher_Log;
using namespace appbox::test;

namespace
{

/**
 * @brief Level the cases which check the log files of a run ask for.
 *
 * The cases look for the message which reports the configuration the launcher
 * loaded and for the message which reports an initialized sandbox, and both are
 * reported at `info`. A case which pinned the level of the run instead would
 * fail while a run which asks for `warn` is a legitimate run.
 */
constexpr const wchar_t* kCaseLogLevel = L"info";

/**
 * @brief Message the launcher writes into its own log file.
 *
 * Only the launcher writes it, so it proves that the log directory holds the
 * log of the launcher itself.
 */
constexpr const char* kLauncherMessage = "Load config:";

/**
 * @brief Message the sandbox writes into the log file of every process.
 *
 * Only a process the sandbox was injected into writes it, so it proves that the
 * log directory holds the log of a process of the sandbox.
 */
constexpr const char* kSandboxMessage = "AppBox Sandbox initialized for ";

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
 * @brief Append the startup file of a case.
 *
 * The arguments of the file carry its marker, which the probe process of the
 * case reports back: the marker proves that the launcher started the file, so a
 * case can tell a run which wrote no log from a run which did not happen.
 *
 * @param[in,out] launcher_config Configuration to extend.
 */
void AddStartup(appbox::LauncherConfig& launcher_config)
{
    appbox::LauncherStartup startup;
    startup.trigger = "app";
    startup.auto_start = true;
    startup.arguments = { "--startup_marker", "app" };
    launcher_config.startups.push_back(std::move(startup));
}

/**
 * @brief Collect the log files of a directory.
 * @param[in] dir Directory to search, which does not have to exist.
 * @return The paths of the log files, in name order.
 */
std::vector<std::filesystem::path> LogFilesIn(const std::filesystem::path& dir)
{
    std::vector<std::filesystem::path> logs;
    std::error_code                    ec;

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (entry.is_regular_file(ec) && entry.path().extension() == L".log")
        {
            logs.push_back(entry.path());
        }
    }

    std::sort(logs.begin(), logs.end());
    return logs;
}

/**
 * @brief Collect every log file below a directory.
 * @param[in] root Directory to search.
 * @return The paths of the log files, in name order.
 */
std::vector<std::filesystem::path> LogFilesBelow(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> logs;
    std::error_code                    ec;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
    {
        if (entry.is_regular_file(ec) && entry.path().extension() == L".log")
        {
            logs.push_back(entry.path());
        }
    }

    std::sort(logs.begin(), logs.end());
    return logs;
}

/**
 * @brief Collect the log files of a case which its log directory does not hold.
 * @param[in] root Root directory of the case.
 * @return The paths of the log files which landed outside the log directory.
 */
std::vector<std::filesystem::path> StrayLogFiles(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> stray;
    const auto                         dir = root / kCaseLogDirName;

    for (const auto& file : LogFilesBelow(root))
    {
        if (file.parent_path() != dir)
        {
            stray.push_back(file);
        }
    }

    return stray;
}

/**
 * @brief Collect the files a listing carries which another one does not.
 * @param[in] before Files the directory held before the run.
 * @param[in] after Files the directory holds afterwards.
 * @return The paths of the added files, in the order of the second listing.
 */
std::vector<std::filesystem::path> AddedFiles(const std::vector<std::filesystem::path>& before,
                                              const std::vector<std::filesystem::path>& after)
{
    std::vector<std::filesystem::path> added;

    for (const auto& file : after)
    {
        if (std::find(before.begin(), before.end(), file) == before.end())
        {
            added.push_back(file);
        }
    }

    return added;
}

/**
 * @brief Read log files as one text.
 * @param[in] files Paths of the files to read.
 * @return The content of the files, in the order they are given.
 */
std::string ReadLogs(const std::vector<std::filesystem::path>& files)
{
    std::string text;

    for (const auto& file : files)
    {
        std::ifstream stream(file, std::ios::binary);
        if (!stream.is_open())
        {
            continue;
        }

        text.append(std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()));
    }

    return text;
}

/**
 * @brief Directory the launcher writes its log files into by default.
 *
 * The default is `log` beside the launcher program, which is the directory the
 * run of a case reaches when it names no directory of its own.
 *
 * @return The path of the default log directory.
 */
std::filesystem::path DefaultLogDir()
{
    return std::filesystem::path(appbox::test::LauncherPath()).parent_path() / kCaseLogDirName;
}

} // namespace

/**
 * Condition:
 * 1. The launcher runs without a log level and without the option which names
 *    the directory of the log files, so the run asks for no log at all.
 * 2. The configuration holds one auto start file.
 *
 * Expected:
 * 1. The startup file is started, so the run itself took place.
 * 2. The run writes no log file: neither the launcher nor a process of its
 *    sandbox leaves a file behind, and the log directory is not created.
 */
TEST_F(E2E_Launcher_Log, NoLogOptionsWritesNoLogFile)
{
    auto launcher_config = BuildConfig(GetCWD());
    AddStartup(launcher_config);

    LogRun log;
    log.pass_level = false;
    log.pass_dir = false;

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "", false, log);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    const auto logs = LogFilesBelow(GetCWD());
    EXPECT_TRUE(logs.empty()) << "the run wrote " << logs.size() << " log file(s)";
    EXPECT_FALSE(std::filesystem::exists(GetCWD() / kCaseLogDirName));
}

/**
 * Condition:
 * 1. The launcher runs with `--X-AppBox-LogLevel off` and with the option which
 *    names the directory of the log files, so the run is told where to write a
 *    log and still asks for none.
 * 2. The configuration holds one auto start file.
 *
 * Expected:
 * 1. The startup file is started, so the run itself took place.
 * 2. The run writes no log file and creates no log directory, exactly like a
 *    run which names no level at all.
 */
TEST_F(E2E_Launcher_Log, LogLevelOffWritesNoLogFile)
{
    auto launcher_config = BuildConfig(GetCWD());
    AddStartup(launcher_config);

    LogRun log;
    log.level = L"off";

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "", false, log);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    const auto logs = LogFilesBelow(GetCWD());
    EXPECT_TRUE(logs.empty()) << "the run wrote " << logs.size() << " log file(s)";
    EXPECT_FALSE(std::filesystem::exists(GetCWD() / kCaseLogDirName));
}

/**
 * Condition:
 * 1. The launcher runs with a log level and with the log directory of the case.
 * 2. The configuration holds one auto start file, which the launcher starts as
 *    the probe process inside the sandbox.
 *
 * Expected:
 * 1. The startup file is started.
 * 2. The log directory of the case holds the log of the launcher itself and the
 *    log of the process of its sandbox: the message which reports the loaded
 *    configuration and the message which reports an initialized sandbox are
 *    both in the log files of the directory.
 * 3. No log file lands outside the log directory of the case.
 */
TEST_F(E2E_Launcher_Log, LogDirHoldsTheLogsOfTheRun)
{
    auto launcher_config = BuildConfig(GetCWD());
    AddStartup(launcher_config);

    LogRun log;
    log.level = kCaseLogLevel;

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "", false, log);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    const auto dir = GetCWD() / kCaseLogDirName;
    const auto files = LogFilesIn(dir);
    const auto text = ReadLogs(files);

    EXPECT_FALSE(files.empty()) << "the log directory of the run holds no log file";
    EXPECT_NE(text.find(kLauncherMessage), std::string::npos) << "the launcher wrote no log of its own";
    EXPECT_NE(text.find(kSandboxMessage), std::string::npos) << "no process of the sandbox wrote a log";

    const auto stray = StrayLogFiles(GetCWD());
    EXPECT_TRUE(stray.empty()) << "the run wrote " << stray.size() << " log file(s) outside the log directory";
}

/**
 * Condition:
 * 1. The launcher runs with a log level and without the option which names the
 *    directory of the log files, so the default directory applies.
 * 2. The configuration holds one auto start file.
 *
 * Expected:
 * 1. The startup file is started.
 * 2. The run writes its log files into the `log` directory beside the launcher
 *    program, and no file of the working directory of the case carries a log.
 * 3. The case removes the files it added, so the directory of the launcher
 *    keeps the log files of the runs which were there before it.
 */
TEST_F(E2E_Launcher_Log, TheDefaultLogDirectoryIsBesideTheLauncher)
{
    auto launcher_config = BuildConfig(GetCWD());
    AddStartup(launcher_config);

    const auto dir = DefaultLogDir();
    const auto before = LogFilesIn(dir);

    LogRun log;
    log.pass_dir = false;
    log.level = kCaseLogLevel;

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "", false, log);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    const auto added = AddedFiles(before, LogFilesIn(dir));
    const auto text = ReadLogs(added);

    EXPECT_FALSE(added.empty()) << "the run wrote no log file beside the launcher";
    EXPECT_NE(text.find(kLauncherMessage), std::string::npos) << "the launcher wrote no log of its own";

    const auto stray = StrayLogFiles(GetCWD());
    EXPECT_TRUE(stray.empty()) << "the run wrote " << stray.size() << " log file(s) into the case";

    std::error_code ec;
    for (const auto& file : added)
    {
        std::filesystem::remove(file, ec);
    }
    /* The call removes the directory only while it holds nothing else. */
    std::filesystem::remove(dir, ec);
}

/**
 * Condition:
 * 1. The launcher runs with a log level and with a relative log directory,
 *    which is resolved against the current directory of the launcher.
 * 2. The launcher is started without a working directory of its own, so its
 *    current directory is the one of the test executable.
 *
 * Expected:
 * 1. The startup file is started.
 * 2. The run writes its log files into the relative directory below the current
 *    directory of the test executable, and not into the working directory of
 *    the case.
 * 3. The case removes the directory it created.
 */
TEST_F(E2E_Launcher_Log, ARelativeLogDirectoryIsResolvedAgainstTheCurrentDirectory)
{
    auto launcher_config = BuildConfig(GetCWD());
    AddStartup(launcher_config);

    /* A single name, so the launcher resolves it against its current directory. */
    const auto name = GetCWD().filename().wstring();
    const auto expected = std::filesystem::current_path() / name;

    LogRun log;
    log.dir = name;
    log.level = kCaseLogLevel;

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "", false, log);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    const auto files = LogFilesIn(expected);
    const auto text = ReadLogs(files);

    EXPECT_FALSE(files.empty()) << "the run wrote no log file into '" << appbox::WideToUTF8(expected.wstring()) << "'";
    EXPECT_NE(text.find(kLauncherMessage), std::string::npos) << "the launcher wrote no log of its own";
    EXPECT_FALSE(std::filesystem::exists(GetCWD() / kCaseLogDirName));

    std::error_code ec;
    std::filesystem::remove_all(expected, ec);
}
