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
constexpr wchar_t kFolderName[] = L"AppBoxTest_MergeCreate";

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
 * 1. The folder is isolated with `Merge`, a lower layer holds the folder
 *    `fresh` with a file of its own, and the host filesystem holds neither
 *    `fresh` nor a file inside it.
 * 2. The sandboxed process creates the file `fresh\new.txt`.
 *
 * Expected:
 * 1. The file is created in the host filesystem, which also gains the folder
 *    `fresh` it does not hold yet: `Merge` creates the folders of the entry it
 *    writes.
 * 2. The overlay holds neither the folder nor the file, and the content of the
 *    lower layer stays in its layer.
 */
TEST_F(E2E_Fs, Merge_CreatesTheFoldersOfANewFileInTheHost)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsDir(L"fresh", {
                        FsFile(L"keep.txt", "packed")
                    })
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
                       appbox::FilesystemIsolation::Merge }
    }));

    /* The entry no layer holds is created in the host filesystem. */
    {
        ProtocolWriteFile::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\fresh\\new.txt");
        req.Data = "made";
        req.dwCreationDisposition = CREATE_ALWAYS;

        const auto rsp = ProbeWriteFile.Call(req, GetCWD(), config).get<ProtocolWriteFile::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "made");
    }

    /* The host filesystem gained the folder and the file. */
    {
        EXPECT_TRUE(host.FileExists(L"fresh\\new.txt"));

        std::string data;
        ASSERT_EQ(ReadFileFull((host.Get() / L"fresh" / L"new.txt").wstring(), data),
                  static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "made");
    }

    /* The overlay holds none of them, so the sandbox keeps no copy. */
    EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\fresh"));
    ASSERT_TRUE(tree.Verify());
}
