#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/CreateFileW.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

/**
 * Condition:
 * 1. File exists in the state of the sandbox only.
 * 2. The layer `#USERPROFILE#` of the case is pinned to `Write Copy`, so the
 *    delete stays in the sandbox and the case pins its own rule instead of the
 *    default of the view.
 * 3. Open the file with `CreateFileW` and the flag which removes it when the
 *    handle is closed.
 *
 * Expected:
 * 1. The open succeeds and the close removes the file of the state.
 * 2. No whiteout file in the state of the sandbox, because no lower layer
 *    holds the name.
 */
TEST_F(E2E_Fs, DeleteOnClose_UpperOnly)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {
            FsDir(L"filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true), {
                FsFile(L"data.txt", "hello")
            })
        }),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {})
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

    /* The file is gone from the state of the sandbox. */
    {
        auto fPath =
            GetCWDString() + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) + L"\\data.txt";
        ASSERT_FALSE(std::filesystem::exists(fPath));
    }

    /* Whiteout file should not exist in the state of the sandbox. */
    {
        auto fPath = GetCWDString() + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) +
                     L"\\data.txt.$APPBOX_DELETE$";
        ASSERT_FALSE(std::filesystem::exists(fPath));
    }

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
