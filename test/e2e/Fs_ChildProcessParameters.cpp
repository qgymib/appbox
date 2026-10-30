#include "probe/LaunchProcess.hpp"
#include "probe/ParamCheck.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbePath.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <filesystem>
#include <spdlog/fmt/fmt.h>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name the probe of a case runs under inside the view. */
constexpr const wchar_t* kProbeName = L"probe.exe";

/**
 * @brief Build the tree of a case: the probe of the run inside the profile of
 *        the view.
 *
 * @param[in] cwd The working directory of the case.
 * @return The tree, which the caller turns into the configuration of the run.
 */
FsRoot BuildTree(const std::filesystem::path& cwd)
{
    FsNode::Bytes probe_bytes;
    EXPECT_EQ(ReadFileFull(ProbePath(ProbeBitness::Bit64), probe_bytes), static_cast<DWORD>(0));
    EXPECT_FALSE(probe_bytes.empty());

    /* clang-format off */
    return FsRoot(cwd, {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsNode(kProbeName, probe_bytes)
            })
        })
    });
    /* clang-format on */
}

/**
 * @brief Describe the run for the message of an assertion.
 *
 * @param[in] api The entry point the case used.
 * @param[in] rsp The report of the probe which launched the child.
 * @param[in] expected_image Image path the child had to report.
 * @param[in] expected_directory Working directory the child had to report.
 * @return The description of the run.
 */
std::string FormatReport(LaunchApi api, const ProtocolLaunchProcess::Rsp& rsp, const std::wstring& expected_image,
                         const std::wstring& expected_directory)
{
    return fmt::format("api={} launch={} exit={:#x} expected image='{}' expected directory='{}'",
                       api == LaunchApi::RtlCreateUserProcess ? "RtlCreateUserProcess" : "CreateProcess", rsp.code,
                       rsp.exit_code, appbox::WideToUTF8(expected_image), appbox::WideToUTF8(expected_directory));
}

/**
 * @brief Ask the child to report its parameters and check them.
 *
 * @param[in] cwd The working directory of the case.
 * @param[in] config The configuration of the run.
 * @param[in] api The entry point of the creation.
 * @param[in] request The request which names the child.
 * @param[in] expected_image Image path the child has to report.
 * @param[in] expected_directory Working directory the child has to report.
 */
void ExpectChildParameters(const std::filesystem::path& cwd, const appbox::LauncherConfig& launcher_config,
                           LaunchApi api, ProtocolLaunchProcess::Req request, const std::wstring& expected_image,
                           const std::wstring& expected_directory)
{
    request.Api = api;
    request.Arguments = { "paramcheck", "--image", appbox::WideToUTF8(expected_image) };
    if (!expected_directory.empty())
    {
        request.Arguments.push_back("--cwd");
        request.Arguments.push_back(appbox::WideToUTF8(expected_directory));
    }

    const auto rsp = Probe(cwd.wstring(), launcher_config).Call<ProtocolLaunchProcess>(request);
    const auto report = FormatReport(api, rsp, expected_image, expected_directory);

    ASSERT_EQ(rsp.code, static_cast<DWORD>(0)) << report;
    ASSERT_EQ(rsp.exit_code & ~kParamCheckMask, static_cast<DWORD>(0))
        << "the child did not reach its own report: " << report;

    EXPECT_EQ(rsp.exit_code & ParamCheckImageMatched, static_cast<DWORD>(ParamCheckImageMatched))
        << "the child reports another image path: " << report;
    if (!expected_directory.empty())
    {
        EXPECT_EQ(rsp.exit_code & ParamCheckCurrentDirectoryMatched,
                  static_cast<DWORD>(ParamCheckCurrentDirectoryMatched))
            << "the child reports another working directory: " << report;
    }
    EXPECT_EQ(rsp.exit_code & ParamCheckSandboxLoaded, static_cast<DWORD>(ParamCheckSandboxLoaded))
        << "the child is not sandboxed: " << report;
}

} // namespace

/**
 * Condition:
 * 1. The probe of the run exists inside the profile of the view.
 * 2. A sandboxed probe starts it as a child process through both entry points
 *    of a creation, and names the working directory of the view.
 *
 * Expected:
 * 1. The child is created and sandboxed, so a creation which never passes the
 *    Win32 entry point is injected as well.
 * 2. The image path of the child is the path of the view and not the path of
 *    the layer which holds the file.
 */
TEST_F(E2E_Fs, ChildProcessParameters_OfTheView)
{
    auto tree = BuildTree(GetCWD());
    auto launcher_config = tree.Build();

    const std::wstring profile = GetKnownFolderPath(L"#USERPROFILE#", false);
    const std::wstring image = profile + L"\\" + kProbeName;

    for (const auto api : { LaunchApi::CreateProcess, LaunchApi::RtlCreateUserProcess })
    {
        ProtocolLaunchProcess::Req req;
        req.FileName = appbox::WideToUTF8(image);

        ExpectChildParameters(GetCWD(), launcher_config, api, req, image, std::wstring());
    }

    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. The probe of the run exists inside the profile of the view.
 * 2. A sandboxed probe holds the directory of the layer which backs the
 *    profile, so a child inherits the path of the layer and not the path of the
 *    view.
 * 3. The probe starts the child without a working directory of its own.
 *
 * Expected:
 * 1. The child is sandboxed.
 * 2. The child reports the working directory of the view, so the sandbox maps
 *    the path of the layer back to the path the view spells.
 */
TEST_F(E2E_Fs, ChildProcessParameters_InheritedFromALayer)
{
    auto tree = BuildTree(GetCWD());
    auto launcher_config = tree.Build();

    const std::wstring profile = GetKnownFolderPath(L"#USERPROFILE#", false);
    const std::wstring image = profile + L"\\" + kProbeName;
    const std::wstring layer = (GetCWD() / L"app" / L"filesystem" / L"#USERPROFILE#").wstring();

    for (const auto api : { LaunchApi::CreateProcess, LaunchApi::RtlCreateUserProcess })
    {
        ProtocolLaunchProcess::Req req;
        req.FileName = appbox::WideToUTF8(image);
        req.ParentCurrentDirectory = appbox::WideToUTF8(layer);

        ExpectChildParameters(GetCWD(), launcher_config, api, req, image, profile);
    }

    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. The probe of the run exists inside the profile of the view, and the view
 *    holds the profile as the **root** of its layer, so the layer itself holds
 *    no folder above the profile.
 * 2. A sandboxed probe names that directory as the working directory of a
 *    child, through both entry points of a creation.
 *
 * Expected:
 * 1. The child is created and sandboxed through both entry points: a query of
 *    the directory reports the directory of the view, so the Win32 creation
 *    code finds the working directory it was given even though the layer of
 *    the profile is mounted at that directory.
 * 2. The image path of the child is the path of the view and not the path of
 *    the layer.
 * 3. The child reports the working directory of the view.
 */
TEST_F(E2E_Fs, ChildProcessParameters_LayerRootAsWorkingDirectory)
{
    auto tree = BuildTree(GetCWD());
    auto launcher_config = tree.Build();

    const std::wstring profile = GetKnownFolderPath(L"#USERPROFILE#", false);
    const std::wstring image = profile + L"\\" + kProbeName;

    for (const auto api : { LaunchApi::CreateProcess, LaunchApi::RtlCreateUserProcess })
    {
        ProtocolLaunchProcess::Req req;
        req.FileName = appbox::WideToUTF8(image);
        req.CurrentDirectory = appbox::WideToUTF8(profile);

        ExpectChildParameters(GetCWD(), launcher_config, api, req, image, profile);
    }

    ASSERT_TRUE(tree.Verify());
}
