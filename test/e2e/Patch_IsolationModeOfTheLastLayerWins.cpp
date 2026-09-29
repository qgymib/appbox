#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Patch;
using namespace appbox::test;

namespace
{

/** Folder of the case which the patch package isolates. */
constexpr wchar_t kFolderOfThePatch[] = L"AppBoxTest_PatchIsolatedByPatch";

/** Folder of the case which only the archive isolates. */
constexpr wchar_t kFolderOfTheApp[] = L"AppBoxTest_PatchIsolatedByApp";

/**
 * @brief Build the virtual path of a folder of the case.
 * @param[in] name Name of the folder below the user profile.
 * @return The path of the folder in the virtual filesystem.
 */
std::wstring VirtualPathOf(const wchar_t* name)
{
    return std::wstring(L"#USERPROFILE#\\") + name;
}

/**
 * @brief Read a file of a folder below the user profile inside the sandbox.
 * @param[in] folder Path of the folder of the case.
 * @param[in] name Name of the file.
 * @param[in] cwd Working directory of the case.
 * @param[in] config Configuration of the case.
 * @return The response of the probe.
 */
ProtocolReadFileFull::Rsp ReadHostFile(const std::wstring& folder, const wchar_t* name,
                                       const std::filesystem::path& cwd, appbox::LoaderConfig& config)
{
    ProtocolReadFileFull::Req req;
    req.FileName = appbox::WideToUTF8(folder + L"\\" + name + L"\\host.txt");
    return ProbeReadFileFull.Call(req, cwd, config).get<ProtocolReadFileFull::Rsp>();
}

} // namespace

/**
 * Condition:
 * 1. Two folders of the host exist, each with a file of its own.
 * 2. The isolation file of the archive keeps the first folder visible and
 *    hides the second one, because a mode of the archive reaches the paths the
 *    patches do not name.
 * 3. `01-bar.zip` hides the first folder as well.
 * 4. The sandboxed process reads the host file of both folders.
 *
 * Expected:
 * 1. Both reads report `File Not Found`, because the mode of the folder the
 *    package names is the mode of the package and the mode of the folder it
 *    does not name stays the mode of the archive.
 * 2. The host folders are untouched.
 */
TEST_F(E2E_Patch, IsolationModeOfTheLastLayerWins)
{
    RealFsFolder folder_of_the_patch(L"#USERPROFILE#", kFolderOfThePatch);
    ASSERT_TRUE(folder_of_the_patch.WriteFile(L"host.txt", "host"));

    RealFsFolder folder_of_the_app(L"#USERPROFILE#", kFolderOfTheApp);
    ASSERT_TRUE(folder_of_the_app.WriteFile(L"host.txt", "host"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem", {
                FsDir(L"#USERPROFILE#", {})
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    /* The archive keeps the host folders visible. */
    ASSERT_TRUE(
        WriteFsIsolationFile(GetCWD(), {
                                           { VirtualPathOf(kFolderOfThePatch), appbox::FilesystemEntryKind::Directory,
                                            appbox::FilesystemIsolation::WriteCopy },
                                           { VirtualPathOf(kFolderOfTheApp),   appbox::FilesystemEntryKind::Directory,
                                            appbox::FilesystemIsolation::Full      },
    }));

    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));
    ASSERT_TRUE(WritePatchPackage(patch_dir / L"01-bar.zip",
                                  {
    },
                                  {
                                      { VirtualPathOf(kFolderOfThePatch), appbox::FilesystemEntryKind::Directory,
                                        appbox::FilesystemIsolation::Full },
                                  }));

    const auto folder = GetKnownFolderPath(L"#USERPROFILE#", false);

    /* The mode of the folder the package names is the mode of the package. */
    {
        const auto rsp = ReadHostFile(folder, kFolderOfThePatch, GetCWD(), config);
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The mode of a folder no package names stays the mode of the archive. */
    {
        const auto rsp = ReadHostFile(folder, kFolderOfTheApp, GetCWD(), config);
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The host folders were not touched. */
    EXPECT_TRUE(folder_of_the_patch.FileExists(L"host.txt"));
    EXPECT_TRUE(folder_of_the_app.FileExists(L"host.txt"));
    ASSERT_TRUE(tree.Verify());
}
