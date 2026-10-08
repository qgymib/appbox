#include "probe/DeleteFileW.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

/**
 * Condition:
 * 1. File exists in the lower layer of the resources only.
 * 2. The layer `#USERPROFILE#` of the case is pinned to `Write Copy`, so the
 *    delete stays in the sandbox and the case pins its own rule instead of the
 *    default of the view.
 * 3. Try to delete file.
 *
 * Expected:
 * 1. Delete file success.
 * 2. File not exists in the state of the sandbox.
 * 3. Whiteout file exists in the state of the sandbox.
 * 4. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, DeleteFile_LowerLayer)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsFile(L"data.txt", "hello1")
            })
        })
    });
    /* clang-format on */

    /* Build filesystem tree. */
    auto config = tree.Build();

    /*
     * The layer of the case is pinned to `Write Copy`, which is the mode a
     * path had before the default of the view became `Merge`: the case pins
     * the rule it is about and never reaches the host filesystem.
     */
    ASSERT_TRUE(WriteFsIsolationFile(GetCWD(), {
                                                   { L"#USERPROFILE#", appbox::FilesystemEntryKind::Directory,
                                                    appbox::FilesystemIsolation::WriteCopy }
    }));

    /* Delete file. */
    {
        ProtocolDeleteFileW::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\data.txt");

        auto rsp = ProbeDeleteFileW.Call(req, GetCWD(), config).get<ProtocolDeleteFileW::Rsp>();
        ASSERT_EQ(rsp.code, 0);
    }

    /* File should not exist in the state of the sandbox. */
    {
        auto fPath =
            GetCWDString() + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) + L"\\data.txt";
        ASSERT_FALSE(std::filesystem::exists(fPath));
    }

    /* Whiteout file should exist in the state of the sandbox. */
    {
        auto fPath = GetCWDString() + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) +
                     L"\\data.txt.$APPBOX_DELETE$";
        ASSERT_TRUE(std::filesystem::exists(fPath));
    }

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
