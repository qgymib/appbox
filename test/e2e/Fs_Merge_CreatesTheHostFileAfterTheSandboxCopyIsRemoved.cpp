#include "probe/DeleteFileW.hpp"
#include "probe/WriteFile.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_MergeCopyRemoved";

/**
 * @brief Get the path of the folder of the case inside the view.
 * @return Path of the folder.
 */
std::wstring FolderPath()
{
    return GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
}

/**
 * @brief Get the path of the folder of the case inside the overlay.
 * @param[in] cwd Working directory of the case.
 * @return Path of the folder in the overlay.
 */
std::wstring FolderInOverlay(const std::wstring& cwd)
{
    return cwd + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) + L"\\" + kFolderName;
}

} // namespace

/**
 * Condition:
 * 1. The overlay carries the copy of `data.txt` an earlier `Write Copy` run
 *    left there, and the host filesystem holds a file of the same name.
 * 2. The folder is isolated with `Merge`.
 * 3. The first run deletes the file, which removes the copy and records the
 *    delete inside the sandbox; the second run creates the file again.
 *
 * Expected:
 * 1. The delete of the first run leaves the file of the host filesystem in
 *    place, because a sandbox layer held the entry.
 * 2. The second run removes the marker of the delete, which leaves no sandbox
 *    layer holding the entry: the create reaches the host filesystem, which
 *    carries the new content afterwards, and the overlay keeps no copy.
 */
TEST_F(E2E_Fs, Merge_CreatesTheHostFileAfterTheSandboxCopyIsRemoved)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"data.txt", "host"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {
            FsDir(L"filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true), {
                FsDir(kFolderName, {
                    FsFile(L"data.txt", "copied")
                })
            })
        }),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {})
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
                       appbox::FilesystemIsolation::Merge }
    }));

    /* The first run deletes the copy of the sandbox. */
    {
        ProtocolDeleteFileW::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\data.txt");

        const auto rsp = ProbeDeleteFileW.Call(req, GetCWD(), config).get<ProtocolDeleteFileW::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    /* The delete stayed inside the sandbox. */
    EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\data.txt"));
    EXPECT_TRUE(host.FileExists(L"data.txt"));

    /* The second run creates the entry, which no sandbox layer holds now. */
    {
        ProtocolWriteFile::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\data.txt");
        req.Data = "written";
        req.dwCreationDisposition = CREATE_ALWAYS;

        const auto rsp = ProbeWriteFile.Call(req, GetCWD(), config).get<ProtocolWriteFile::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "written");
    }

    /* The host file carries the new content and the overlay holds no copy. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull((host.Get() / L"data.txt").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "written");

        EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\data.txt"));
        EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\data.txt.$APPBOX_DELETE$"));
    }

    ASSERT_TRUE(tree.Verify());
}
