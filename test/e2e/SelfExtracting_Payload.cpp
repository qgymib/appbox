#include "utils/CommonFixture.hpp"
#include "utils/LauncherPath.hpp"
#include "utils/PatchBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/RunState.hpp"
#include "utils/SelfExtractingProgram.hpp"
#include "src/core/PackService.hpp"
#include "SandboxId.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

typedef appbox::test::CommonFixture E2E_SelfExtracting;
using namespace appbox::test;

namespace
{

/** Name of the program the cases pack. */
constexpr const wchar_t* kProgramName = appbox::test::kSelfExtractingProgramName;

/** Marker the probe process reports back; it tells that the payload ran. */
constexpr const char* kMarker = "sfx";

/** Number of attempts of a check which waits for the run to finish its work. */
constexpr int kSettleAttempts = 40;

/** Delay between two attempts of a check, in milliseconds. */
constexpr int kSettleDelayMs = 50;

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
    ASSERT_TRUE(PackProbeSelfExtracting(GetCWD(), L"app", appbox::StartupScreen{ false }, error)) << error;

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
    ASSERT_TRUE(PackProbeSelfExtracting(GetCWD(), L"app", appbox::StartupScreen{ false }, error)) << error;

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
 * 1. The run exits with zero and the probe of the payload reports its marker.
 * 2. The payload was extracted into a temporary folder of the run, which the
 *    state of the run reports.
 * 3. The startup screen covered the run: it was shown and closed before the
 *    launcher returned.
 */
TEST_F(E2E_SelfExtracting, TheStartupScreenCoversTheExtraction)
{
    std::string error;
    std::string program_id;
    ASSERT_TRUE(PackProbeSelfExtracting(GetCWD(), L"app", appbox::StartupScreen{}, error, &program_id)) << error;
    ASSERT_TRUE(appbox::IsValidSandboxId(program_id)) << program_id;

    StartupRunHandle run(GetCWDString(), (GetCWD() / kProgramName).wstring(), "", kMarker);
    ASSERT_TRUE(run.WaitForSessions(kProbeBindQuietMs, kProbeBindTimeoutMs)) << "the run never started the payload";

    const auto& reports = run.Collect();
    EXPECT_EQ(reports.started, std::vector<std::string>({ kMarker }));
    ASSERT_EQ(reports.reports.size(), static_cast<std::size_t>(1));

    /*
     * The state of the run reports the screen of the session and the folder the
     * payload was extracted into: the screen covers the extraction, which is the
     * temporary folder of the run, and it was closed once the payload started.
     */
    appbox::MsgRunState::Rsp state;
    ASSERT_TRUE(QueryRunState(program_id, state)) << "the state of the run cannot be read";

    EXPECT_TRUE(state.screen_shown);
    EXPECT_TRUE(state.screen_closed);
    ASSERT_FALSE(state.extraction_root.empty()) << "the run reported no extraction folder";

    const auto root = Lower(StripTrailingSeparators(appbox::UTF8ToWide(state.extraction_root)));
    const auto temporary = Lower(StripTrailingSeparators(std::filesystem::temp_directory_path().wstring()));
    EXPECT_EQ(root.rfind(temporary, 0), static_cast<std::size_t>(0)) << state.extraction_root;

    run.Exit(0);
    EXPECT_EQ(run.Wait(), 0u);
}
