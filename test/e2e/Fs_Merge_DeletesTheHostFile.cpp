#include "probe/DeleteFileW.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
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
constexpr wchar_t kFolderName[] = L"AppBoxTest_MergeDelete";

/**
 * @brief Get the path of the folder of the case inside the view.
 * @return Path of the folder.
 */
std::wstring FolderPath()
{
    return GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
}

} // namespace

/**
 * Condition:
 * 1. The folder is isolated with `Merge` and the host filesystem holds the file
 *    `data.txt`, which no layer of the sandbox carries.
 * 2. The sandboxed process deletes the file.
 *
 * Expected:
 * 1. The delete is applied to the host filesystem: the file is really removed
 *    from it, because `Merge` applies a modification to the layer it names.
 * 2. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, Merge_DeletesTheFileOfTheHost)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"data.txt", "host"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"packed.txt", "packed")
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

    /* The delete of the entry of the host filesystem succeeds. */
    {
        ProtocolDeleteFileW::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\data.txt");

        const auto rsp = ProbeDeleteFileW.Call(req, GetCWD(), config).get<ProtocolDeleteFileW::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    /* The entry is gone from the host filesystem. */
    EXPECT_FALSE(host.FileExists(L"data.txt"));

    /* The entry of the lower layer stays in its layer. */
    ASSERT_TRUE(tree.Verify());
}
