#include "probe/DeleteFileW.hpp"
#include "utils/ProbeCall.hpp"
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
constexpr wchar_t kFolderName[] = L"AppBoxTest_MergeDeleteUpperCopy";

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
 * 3. The sandboxed process deletes the file.
 *
 * Expected:
 * 1. The delete stays inside the sandbox, because a sandbox layer holds the
 *    entry: the copy of the overlay is removed and a whiteout marker records
 *    the delete, so the view does not report the entry any more.
 * 2. The file of the host filesystem keeps its own content.
 */
TEST_F(E2E_Fs, Merge_DeletesTheSandboxCopyAndKeepsTheHostFile)
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

    /* The delete of the entry a sandbox layer holds succeeds. */
    {
        ProtocolDeleteFileW::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\data.txt");

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolDeleteFileW>(req);
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    /* The copy of the overlay is gone and the marker records the delete. */
    EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\data.txt"));
    EXPECT_TRUE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\data.txt.$APPBOX_DELETE$"));

    /* The file of the host filesystem keeps its own content. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull((host.Get() / L"data.txt").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "host");
    }

    ASSERT_TRUE(tree.Verify());
}
