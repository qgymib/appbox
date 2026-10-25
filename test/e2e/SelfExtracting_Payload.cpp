#include "utils/CommonFixture.hpp"
#include "utils/LauncherPath.hpp"
#include "utils/PatchBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"
#include "utils/ReadFileFull.hpp"
#include "src/core/PackService.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

typedef appbox::test::CommonFixture E2E_SelfExtracting;
using namespace appbox::test;

namespace
{

/** Name of the program the cases pack. */
constexpr const wchar_t* kProgramName = L"MyApp.exe";

/** Marker the probe process reports back; it tells that the payload ran. */
constexpr const char* kMarker = "sfx";

/** Name of the directory the run of a case writes its log files into. */
constexpr const wchar_t* kLogDirName = appbox::test::kCaseLogDirName;

/** Number of attempts of a check which waits for the run to finish its work. */
constexpr int kSettleAttempts = 40;

/** Delay between two attempts of a check, in milliseconds. */
constexpr int kSettleDelayMs = 50;

/**
 * @brief Read the payload of the launcher of the suite.
 * @return The bytes of the launcher, empty when the run named no launcher.
 */
std::vector<char> ReadLauncherPayload()
{
    const auto& path = LauncherPath();
    if (path.empty())
    {
        return {};
    }

    std::string data;
    if (ReadFileFull(path, data) != 0)
    {
        return {};
    }

    return std::vector<char>(data.begin(), data.end());
}

/**
 * @brief Pack a session which starts the 64 bit probe as a self-extracting
 *        program.
 *
 * The probe is imported as `MyApp\app.exe`, so the program starts it through
 * the layer of the packaged application. The import folder is removed after the
 * pack run, which leaves the program as the only source of the image.
 *
 * @param[in] root Directory the program is written into.
 * @param[in] trigger Trigger of the startup file.
 * @param[in] screen Startup screen of the session.
 * @param[out] error Description of the failure.
 * @return true on success.
 */
bool PackProbeProgram(const std::filesystem::path& root, const std::wstring& trigger,
                      const appbox::StartupScreen& screen, std::string& error)
{
    const auto& probe = ProbePath(ProbeBitness::Bit64);
    if (probe.empty())
    {
        error = "the run was started without the path of the 64 bit probe";
        return false;
    }

    const auto import = root / L"import";
    const auto application = import / L"MyApp";

    std::error_code ec;
    std::filesystem::create_directories(application, ec);
    if (ec)
    {
        error = "failed to create the import folder: " + ec.message();
        return false;
    }

    std::filesystem::copy_file(probe, application / L"app.exe", std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        error = "failed to copy the probe: " + ec.message();
        return false;
    }

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    if (!model.ImportFolder("program_files", application.wstring(), error))
    {
        return false;
    }
    if (!model.RestoreStartupFile("program_files", L"MyApp", L"app.exe", trigger, true, error))
    {
        return false;
    }

    const auto payload = ReadLauncherPayload();
    if (payload.empty())
    {
        error = "failed to read the launcher payload";
        return false;
    }

    appbox::PackPayloads payloads;
    payloads.launcher_bytes = payload.data();
    payloads.launcher_size = payload.size();

    const auto exe_path = root / kProgramName;
    const auto result = appbox::PackSelfExtracting(
        model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(), appbox::EnvironmentModel(),
        appbox::ApplicationMetadata(), screen, payloads, appbox::CompressionLevel::Normal, exe_path.wstring(), nullptr);
    if (!result.empty())
    {
        error = result;
        return false;
    }

    std::filesystem::remove_all(import, ec);
    return true;
}

/**
 * @brief Lower the ASCII letters of a path.
 * @param[in] text Text to fold.
 * @return The text with every upper case letter lowered.
 */
std::wstring Lower(std::wstring text)
{
    for (auto& character : text)
    {
        if (character >= L'A' && character <= L'Z')
        {
            character = static_cast<wchar_t>(character - L'A' + L'a');
        }
    }
    return text;
}

/**
 * @brief Drop the trailing separators of a path.
 * @param[in] text Path to trim.
 * @return The path without trailing separators.
 */
std::wstring StripTrailingSeparators(std::wstring text)
{
    while (!text.empty() && (text.back() == L'\\' || text.back() == L'/'))
    {
        text.pop_back();
    }
    return text;
}

/**
 * @brief Collect the extraction folders a self-extracting program of the case
 *        creates.
 *
 * The launcher extracts the payload into
 * `<temporary directory>\AppBox-SFX-<program stem>-<pid>`, so a folder which
 * still exists after the run would stay behind on the machine of the user.
 *
 * @param[in] stem Stem of the program file name, e.g. `L"MyApp"`.
 * @return The folders which are still there, in name order.
 */
std::vector<std::filesystem::path> ExtractionFolders(const std::wstring& stem)
{
    std::vector<std::filesystem::path> folders;

    const auto prefix = Lower(L"AppBox-SFX-" + stem + L"-");

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::temp_directory_path(), ec))
    {
        if (!entry.is_directory(ec))
        {
            continue;
        }

        const auto name = Lower(entry.path().filename().wstring());
        if (name.rfind(prefix, 0) == 0)
        {
            folders.push_back(entry.path());
        }
    }

    std::sort(folders.begin(), folders.end());
    return folders;
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
 * 1. A session starts the 64 bit probe as `MyApp\app.exe` and is packed as a
 *    self-extracting program into the working directory of the case, so the
 *    resources of the application travel in the payload of the program.
 * 2. The program is started without a configuration file, so it reads the
 *    configuration and the payload of its own image.
 *
 * Expected:
 * 1. The run exits with zero and the probe reports its marker, so the payload
 *    of the program really ran.
 * 2. The image the probe ran is the file of the extraction folder below the
 *    temporary directory: the resource root of the run is a temporary folder.
 * 3. The state of the sandbox is created beside the program, and neither an
 *    `app` directory appears beside it nor a folder stays in the temporary
 *    directory of the machine.
 */
TEST_F(E2E_SelfExtracting, ThePayloadRunsFromATemporaryFolder)
{
    std::string error;
    ASSERT_TRUE(PackProbeProgram(GetCWD(), L"app", appbox::StartupScreen{ false }, error)) << error;

    /* The program carries its resources, so no resource folder is beside it. */
    ASSERT_FALSE(std::filesystem::exists(GetCWD() / appbox::layout::kAppDirNameW));

    const auto exe_path = GetCWD() / kProgramName;
    ASSERT_TRUE(std::filesystem::is_regular_file(exe_path));

    const auto run = ProbeStartupRunEmbedded(GetCWDString(), exe_path.wstring(), "", kMarker);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ kMarker }));

    ASSERT_EQ(run.reports.size(), static_cast<std::size_t>(1));
    const auto module = Lower(appbox::UTF8ToWide(run.reports[0].module_path));
    const auto temp = Lower(StripTrailingSeparators(std::filesystem::temp_directory_path().wstring()));
    EXPECT_EQ(module.rfind(temp, 0), static_cast<std::size_t>(0)) << run.reports[0].module_path;
    EXPECT_NE(module.find(L"app\\filesystem\\#programfiles#\\myapp\\app.exe"), std::wstring::npos)
        << run.reports[0].module_path;

    /* The state stays beside the program, the resources never do. */
    EXPECT_TRUE(std::filesystem::exists(GetCWD() / appbox::layout::kStateDirNameW));
    EXPECT_FALSE(std::filesystem::exists(GetCWD() / appbox::layout::kAppDirNameW));

    /* The extraction of the run is released when the run ends. */
    std::vector<std::filesystem::path> left;
    for (int attempt = 0; attempt < kSettleAttempts; ++attempt)
    {
        left = ExtractionFolders(L"MyApp");
        if (left.empty())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kSettleDelayMs));
    }

    ASSERT_TRUE(left.empty()) << appbox::WideToUTF8(left.front().wstring());
}

/**
 * Condition:
 * 1. A self-extracting program of the case carries the resources of the
 *    application in its payload.
 * 2. The `patch` directory beside the program carries a package which
 *    overrides a file of the filesystem.
 *
 * Expected:
 * 1. The run exits with zero.
 * 2. The package of the directory beside the program was merged into the run:
 *    its cache lives below the state directory of the program.
 * 3. No `app` directory appears beside the program, because the resources of
 *    the run come from the payload.
 */
TEST_F(E2E_SelfExtracting, ThePatchesBesideTheProgramAreMerged)
{
    std::string error;
    ASSERT_TRUE(PackProbeProgram(GetCWD(), L"app", appbox::StartupScreen{ false }, error)) << error;

    constexpr const wchar_t* fileName = L"AppBoxTest_SelfExtracting.txt";

    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));
    ASSERT_TRUE(
        WritePatchPackage(patch_dir / L"00-foo.zip", {
                                                         { std::wstring(L"#USERPROFILE#\\") + fileName, "patched" },
    }));

    const auto run = ProbeStartupRunEmbedded(GetCWDString(), (GetCWD() / kProgramName).wstring(), "", kMarker);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ kMarker }));

    const auto cache = GetCWD() / appbox::layout::kStateDirNameW / appbox::layout::kCacheDirNameW / L"00-foo" /
                       appbox::layout::kFilesystemDirNameW / L"#USERPROFILE#" / fileName;
    EXPECT_TRUE(std::filesystem::is_regular_file(cache)) << appbox::WideToUTF8(cache.wstring());

    EXPECT_FALSE(std::filesystem::exists(GetCWD() / appbox::layout::kAppDirNameW));
}

/**
 * Condition:
 * 1. A self-extracting program of the case is packed with the startup screen of
 *    the session.
 *
 * Expected:
 * 1. The run exits with zero.
 * 2. The log reports that the payload was extracted into a temporary folder of
 *    the run.
 * 3. The startup screen covered the run: it was shown and closed before the
 *    launcher returned.
 */
TEST_F(E2E_SelfExtracting, TheStartupScreenCoversTheExtraction)
{
    std::string error;
    ASSERT_TRUE(PackProbeProgram(GetCWD(), L"app", appbox::StartupScreen{}, error)) << error;

    const auto run = ProbeStartupRunEmbedded(GetCWDString(), (GetCWD() / kProgramName).wstring(), "", kMarker);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ kMarker }));

    const auto log = ReadLog(GetCWD());
    EXPECT_NE(log.find("the payload of '"), std::string::npos) << log;
    EXPECT_NE(log.find(" is extracted to "), std::string::npos) << log;
    EXPECT_NE(log.find("the startup screen is shown"), std::string::npos) << log;
    EXPECT_NE(log.find("the startup screen is closed"), std::string::npos) << log;
}
