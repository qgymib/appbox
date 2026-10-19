#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"
#include "utils/TestKnownFolder.hpp"
#include "launcher/Config.hpp"
#include "sandbox/utils/Defines.hpp"
#include "WString.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Launcher_StartupImage;
using namespace appbox::test;

namespace
{

/** Name of the folder of a case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_StartupImage";

/** Name of the program a case packs into the view. */
constexpr wchar_t kProgramName[] = L"AppBoxProbe64.exe";

/** Content of a file which is not a program image at all. */
constexpr char kNotAProgram[] = "this is not a program image";

/**
 * @brief Get the path of the program of a case inside the view.
 * @return The path of the view, as the configuration spells it.
 */
std::wstring ViewImagePath()
{
    return std::wstring(L"#USERPROFILE#\\") + kFolderName + L"\\" + kProgramName;
}

/**
 * @brief Read a file as the bytes of a resource of a case.
 * @param[in] path Path of the file to read.
 * @return The bytes of the file, empty when it cannot be read.
 */
appbox::test::FsNode::Bytes ReadFileBytes(const std::wstring& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        return {};
    }

    return appbox::test::FsNode::Bytes(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

/**
 * @brief Get the path of the program of a case inside the overlay.
 * @param[in] cwd Working directory of the case.
 * @return The path of the overlay.
 */
std::filesystem::path OverlayImagePath(const std::filesystem::path& cwd)
{
    return cwd / L"data" / L"filesystem" / GetKnownFolderPath(L"#USERPROFILE#", true) / kFolderName / kProgramName;
}

/**
 * @brief Append the startup file of a case.
 * @param[in,out] launcher_config Configuration to extend.
 */
void AddStartup(appbox::LauncherConfig& launcher_config)
{
    appbox::LauncherStartup startup;
    startup.trigger = "app";
    startup.auto_start = true;
    startup.executable = appbox::WideToUTF8(ViewImagePath());
    startup.arguments = { "--startup_marker", "app" };
    launcher_config.startups.push_back(std::move(startup));
}

/**
 * @brief Build the filesystem of a case with one program in the view.
 * @param[in] root Root directory of the filesystem of the case.
 * @param[in] program Content of the program the layer carries.
 * @return The builder of the directories of the case.
 */
appbox::test::FsRoot BuildTree(const std::filesystem::path& root, const appbox::test::FsNode::Bytes& program)
{
    /* clang-format off */
    return appbox::test::FsRoot(root, {
        appbox::test::FsDir(L"data", {}),
        appbox::test::FsDir(L"app", {
            appbox::test::FsDir(L"filesystem\\#USERPROFILE#", {
                appbox::test::FsDir(kFolderName, {
                    appbox::test::FsNode(kProgramName, program)
                })
            })
        })
    });
    /* clang-format on */
}

} // namespace

/**
 * Condition:
 * 1. The layer of the case carries the probe executable below
 *    `#USERPROFILE#\<folder>`, so the program exists in the sandbox view only:
 *    the folder of the host holds no such program.
 * 2. The configuration names that path of the view as the executable of its
 *    only startup file.
 *
 * Expected:
 * 1. The launcher starts the program of the view: it resolves the path of the
 *    view into the host path which holds the image before it creates the
 *    process.
 * 2. The probe reports its marker, so the program really ran inside the
 *    sandbox.
 */
TEST_F(E2E_Launcher_StartupImage, AProgramOfTheViewIsStarted)
{
    const auto bytes = ReadFileBytes(ProbePath(ProbeBitness::Bit64));
    ASSERT_FALSE(bytes.empty()) << "the 64 bit probe could not be read";

    auto tree = BuildTree(GetCWD(), bytes);
    auto launcher_config = tree.Build();
    AddStartup(launcher_config);

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "", false, LogRun{}, true);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));
    ASSERT_EQ(run.reports.size(), 1u);
    EXPECT_NE(run.reports[0].module_path.find(appbox::WideToUTF8(kProgramName)), std::string::npos)
        << run.reports[0].module_path;

    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. The overlay of the case carries the probe executable, the layer carries a
 *    file of the same name which is no program image at all.
 * 2. The configuration names the path of the view which both files answer.
 *
 * Expected:
 * 1. The launcher starts the image of the overlay, because the overlay is the
 *    layer the view prefers: the run succeeds and the probe reports its marker.
 * 2. A run which resolved the image of the layer instead would fail, because
 *    that file is no program.
 */
TEST_F(E2E_Launcher_StartupImage, TheImageOfTheOverlayIsPreferred)
{
    const auto bytes = ReadFileBytes(ProbePath(ProbeBitness::Bit64));
    ASSERT_FALSE(bytes.empty()) << "the 64 bit probe could not be read";

    auto tree = BuildTree(GetCWD(), appbox::test::FsNode::Bytes(kNotAProgram, kNotAProgram + sizeof(kNotAProgram)));
    auto launcher_config = tree.Build();
    AddStartup(launcher_config);

    const auto      image = OverlayImagePath(GetCWD());
    std::error_code ec;
    std::filesystem::create_directories(image.parent_path(), ec);
    ASSERT_FALSE(ec) << ec.message();

    {
        std::ofstream stream(image, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(stream.is_open());
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "", false, LogRun{}, true);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));
}

/**
 * Condition:
 * 1. The layer of the case carries the probe executable below
 *    `#USERPROFILE#\<folder>`.
 * 2. The overlay carries the whiteout marker of that program, which hides the
 *    entry in every layer below it.
 * 3. The configuration names the program as the executable of its only startup
 *    file.
 *
 * Expected:
 * 1. The launcher does not start the program: the marker hides it in the view,
 *    exactly like it hides the entry from a sandboxed process.
 * 2. The run reports a non zero exit code and no probe reported.
 */
TEST_F(E2E_Launcher_StartupImage, AWhiteoutMarkerHidesThePackedProgram)
{
    const auto bytes = ReadFileBytes(ProbePath(ProbeBitness::Bit64));
    ASSERT_FALSE(bytes.empty()) << "the 64 bit probe could not be read";

    auto tree = BuildTree(GetCWD(), bytes);
    auto launcher_config = tree.Build();
    AddStartup(launcher_config);

    const auto      image = OverlayImagePath(GetCWD());
    std::error_code ec;
    std::filesystem::create_directories(image.parent_path(), ec);
    ASSERT_FALSE(ec) << ec.message();

    {
        std::ofstream stream(image.wstring() + APPBOX_SANDBOX_WHITEOUT_SUFFIX_W, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(stream.is_open());
    }

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "", false, LogRun{}, true);

    EXPECT_NE(run.exit_code, 0u);
    EXPECT_TRUE(run.started.empty()) << "the launcher started a program the view hides";
}

/**
 * Condition:
 * 1. The layer of the case carries the probe executable below
 *    `#USERPROFILE#\<folder>`.
 * 2. The isolation file of the case sets the layer to `Whiteout`, which hides
 *    the content of the layer from the view.
 * 3. The configuration names the program as the executable of its only startup
 *    file.
 *
 * Expected:
 * 1. The launcher does not start the program: the isolation mode hides the
 *    layer which holds it, exactly like it hides the entry from a sandboxed
 *    process.
 * 2. The run reports a non zero exit code and no probe reported.
 */
TEST_F(E2E_Launcher_StartupImage, AnIsolationModeHidesThePackedProgram)
{
    const auto bytes = ReadFileBytes(ProbePath(ProbeBitness::Bit64));
    ASSERT_FALSE(bytes.empty()) << "the 64 bit probe could not be read";

    auto tree = BuildTree(GetCWD(), bytes);
    auto launcher_config = tree.Build();
    AddStartup(launcher_config);

    ASSERT_TRUE(WriteFsIsolationFile(GetCWD(), {
                                                   { L"#USERPROFILE#", appbox::FilesystemEntryKind::Directory,
                                                    appbox::FilesystemIsolation::Whiteout }
    }));

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "", false, LogRun{}, true);

    EXPECT_NE(run.exit_code, 0u);
    EXPECT_TRUE(run.started.empty()) << "the launcher started a program the isolation hides";
}
