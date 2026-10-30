#include "probe/WriteFile.hpp"
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
constexpr wchar_t kFolderName[] = L"AppBoxTest_MergeUpperCopy";

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
 * 2. The folder is isolated with `Merge`, which is the mode a later run of the
 *    application observes.
 * 3. The sandboxed process opens the file for writing and writes another
 *    content into it.
 *
 * Expected:
 * 1. The write lands in the copy of the overlay, because a sandbox layer holds
 *    the entry: the read back reports the new content and the copy carries it.
 * 2. The file of the host filesystem keeps its own content, so the mode never
 *    reached it while the sandbox held the entry.
 */
TEST_F(E2E_Fs, Merge_WritesTheSandboxCopyOfTheHostFile)
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

    /* The write reaches the copy the overlay already holds. */
    {
        ProtocolWriteFile::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\data.txt");
        req.Data = "written";

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolWriteFile>(req);
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "written");
    }

    /* The copy of the overlay carries the new content. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull(FolderInOverlay(GetCWDString()) + L"\\data.txt", data),
                  static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "written");
    }

    /* The file of the host filesystem was not touched. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull((host.Get() / L"data.txt").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "host");
    }

    ASSERT_TRUE(tree.Verify());
}
