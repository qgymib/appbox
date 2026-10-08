#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/CreateFileW.hpp"
#include "probe/QueryAttributes.hpp"
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
 * 3. Open the file with `CreateFileW` and the flag which removes it when the
 *    handle is closed.
 *
 * Expected:
 * 1. The open succeeds, and the close removes the copy the sandbox holds.
 * 2. A whiteout file exists in the state of the sandbox, because the entry of
 *    the lower layer stays hidden.
 * 3. The view reports `File Not Found` for the file.
 * 4. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, DeleteOnClose_LowerLayer)
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

    /* Open the file and let the close of the handle delete it. */
    {
        ProtocolCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\data.txt");
        req.dwDesiredAccess = DELETE;
        req.dwCreationDisposition = OPEN_EXISTING;
        req.dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE;

        const auto rsp = ProbeCreateFileW.Call(req, GetCWD(), config).get<ProtocolCreateFileW::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    /* The copy of the file in the state of the sandbox is gone. */
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

    /* The delete hides the entry of the lower layer from the view. */
    {
        ProtocolQueryAttributes::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\data.txt");

        const auto rsp = ProbeQueryAttributes.Call(req, GetCWD(), config).get<ProtocolQueryAttributes::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
