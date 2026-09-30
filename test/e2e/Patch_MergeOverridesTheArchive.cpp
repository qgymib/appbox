#include "probe/WriteFile.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Patch;
using namespace appbox::test;

namespace
{

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_PatchMerge";

/**
 * @brief Get the path of the folder of the case inside the view.
 * @return Path of the folder.
 */
std::wstring FolderPath()
{
    return GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
}

/**
 * @brief Get the virtual path of the folder of the case.
 * @return Path of the folder in the virtual filesystem.
 */
std::wstring VirtualPathOfFolder()
{
    return std::wstring(L"#USERPROFILE#\\") + kFolderName;
}

} // namespace

/**
 * Condition:
 * 1. The isolation file of the archive isolates the folder with `Full`, which
 *    hides the host folder.
 * 2. The patch package `01-bar.zip` names the same path with `Merge`.
 * 3. The host filesystem holds the file `data.txt` of the folder, and the
 *    sandboxed process opens it for writing.
 *
 * Expected:
 * 1. The mode of the last layer which names the path is the mode the sandboxed
 *    process observes: the write reaches the file of the host filesystem.
 * 2. The host file carries the new content.
 */
TEST_F(E2E_Patch, MergeOfTheLastLayerWins)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"data.txt", "host"));

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

    /* The archive hides the host folder. */
    ASSERT_TRUE(WriteFsIsolationFile(GetCWD(), {
                                                   { VirtualPathOfFolder(), appbox::FilesystemEntryKind::Directory,
                                                    appbox::FilesystemIsolation::Full }
    }));

    /* The package merges the folder again. */
    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));
    ASSERT_TRUE(WritePatchPackage(
        patch_dir / L"01-bar.zip",
        {
    },
        {
            { VirtualPathOfFolder(), appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Merge },
        }));

    /* The write of the entry reaches the file of the host filesystem. */
    {
        ProtocolWriteFile::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\data.txt");
        req.Data = "written";

        const auto rsp = ProbeWriteFile.Call(req, GetCWD(), config).get<ProtocolWriteFile::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "written");
    }

    /* The host file carries the new content. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull((host.Get() / L"data.txt").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "written");
    }

    ASSERT_TRUE(tree.Verify());
}
