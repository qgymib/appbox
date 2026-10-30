#include "utils/CommonFixture.hpp"
#include "utils/MultiInstance.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/SelfExtractingProgram.hpp"
#include "utils/TestSandboxId.hpp"
#include "SandboxId.hpp"
#include "WString.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_MultiInstance;
using namespace appbox::test;

namespace
{

/**
 * @brief Collect the log files of a directory.
 *
 * The sandbox names the log file of a process after the image of that process,
 * so the name of a file tells which process wrote it: a case which checks that a
 * process was not injected counts the files whose name starts with the image of
 * that process.
 *
 * @param[in] dir Directory to search, which does not have to exist.
 * @return Paths of the log files, in path order.
 */
std::vector<std::string> LogFilesBelow(const std::filesystem::path& dir)
{
    std::vector<std::string> files;
    std::error_code          ec;

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (entry.is_regular_file(ec) && entry.path().extension() == L".log")
        {
            files.push_back(appbox::WideToUTF8(entry.path().wstring()));
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

/**
 * @brief Whether a path carries a file of the given name anywhere.
 * @param[in] path Path to inspect.
 * @param[in] name Text to look for.
 * @return true when the path carries the text.
 */
bool CarriesName(const std::string& path, const std::string& name)
{
    return path.find(name) != std::string::npos;
}

} // namespace

/**
 * Condition:
 * 1. A sandbox serves a program of its own.
 * 2. The program it starts is the packed launcher of another archive, whose
 *    image carries the sandbox configuration the packer writes with the hooks
 *    turned off.
 *
 * Expected:
 * 1. The packed launcher is not isolated: it serves a sandbox of its own and
 *    starts the program of its configuration, which reports to the case with a
 *    parent other than the master instance of the outer sandbox.
 * 2. The packed launcher writes a log file of its own beside its program, which
 *    is what an isolated launcher does not do.
 * 3. The log directory of the outer sandbox holds no log file of the packed
 *    launcher, because the injection module skipped that process instead of
 *    logging for it.
 */
TEST_F(E2E_MultiInstance, PackedLauncherIsNotInjected)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());

    /*
     * The program of the run is the packed launcher of an archive of its own: it
     * carries the configuration of that archive, the sandbox modules and the
     * configuration of the sandbox with the hooks turned off in its image, so it
     * needs no file beside itself.
     */
    const auto inner = cwd / L"inner";
    ASSERT_TRUE(std::filesystem::create_directories(inner));

    std::string error;
    std::string inner_id;
    ASSERT_TRUE(PackProbeSelfExtracting(inner, L"app", appbox::StartupScreen{ false }, error, &inner_id)) << error;
    ASSERT_TRUE(appbox::IsValidSandboxId(inner_id)) << inner_id;

    const auto program = inner / kSelfExtractingProgramName;
    ASSERT_TRUE(std::filesystem::is_regular_file(program));

    auto launcher_config = MultiInstanceConfig(cwd, id, "app");
    launcher_config.startup.screen = false;

    appbox::LauncherStartup packed;
    packed.trigger = "packed";
    packed.auto_start = false;
    packed.executable = appbox::WideToUTF8(program.wstring());
    packed.arguments = { "--X-AppBox-LogLevel", "info" };
    launcher_config.startups.push_back(std::move(packed));

    /*
     * The case runs the archive of the outer sandbox and waits for it: the run
     * ends with the program of its own configuration, so the case needs no
     * budget of its own.
     */
    const auto master = ProbeStartupRun(GetCWDString(), launcher_config, "packed", false, LogRun{}, true);

    ASSERT_EQ(master.exit_code, 0u);
    ASSERT_EQ(master.reports.size(), static_cast<std::size_t>(1))
        << "the packed launcher started no program of its own sandbox";

    /*
     * The program of the packed archive was started by that launcher and not by
     * the master instance of the outer sandbox, which is the proof that the
     * packed launcher served a sandbox of its own.
     */
    EXPECT_NE(master.reports[0].parent_pid, master.launcher_pid)
        << "the program was started by the outer sandbox instead of the packed launcher";

    /* The packed launcher wrote its own log, so the sandbox left it alone. */
    const auto inner_logs = LogFilesBelow(inner / appbox::test::kCaseLogDirName);
    EXPECT_FALSE(inner_logs.empty()) << "the packed launcher wrote no log file of its own";

    /*
     * An injected process writes its log file into the log directory of the run,
     * named after its image: a file of that name would prove that the injection
     * module logged for the packed launcher.
     */
    const auto program_name = appbox::WideToUTF8(std::filesystem::path(kSelfExtractingProgramName).stem().wstring());
    for (const auto& log : LogFilesBelow(cwd / appbox::test::kCaseLogDirName))
    {
        EXPECT_FALSE(CarriesName(log, program_name)) << "the outer sandbox logged for the packed launcher: " << log;
    }
}
