#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/CreateFileW.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

/**
 * Condition:
 * 1. The lower layer holds the file, the state of the sandbox carries a
 *    whiteout marker for it.
 * 2. Try to create file.
 *
 * Expected:
 * 1. Whiteout file in the state is deleted.
 * 2. New file is created in the state of the sandbox.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, NewFile_WhiteoutInUpper)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {
            FsDir(L"filesystem\\" + GetKnownFolderPath(L"#APPDATA#", true), {
                FsFile(L"data.txt.$APPBOX_DELETE$", "")
            })
        }),
        FsDir(L"app", {
            FsDir(L"filesystem\\#APPDATA#", {
                FsFile(L"data.txt", "hello1")
            })
        })
    });
    /* clang-format on */

    /* Build filesystem tree. */
    auto config = tree.Build();

    /* Create file in the state of the sandbox. */
    {
        ProtocolCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"#APPDATA#", false) + L"\\data.txt");
        req.dwDesiredAccess = GENERIC_WRITE;
        req.dwCreationDisposition = CREATE_NEW;
        auto rsp = ProbeCreateFileW.Call(req, GetCWD(), config).get<ProtocolCreateFileW::Rsp>();
        ASSERT_EQ(rsp.code, 0);
    }

    /* Target file should be created. */
    {
        auto fPath = GetCWDString() + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#APPDATA#", true) + L"\\data.txt";
        ASSERT_TRUE(std::filesystem::exists(fPath));
    }

    /* Whiteout file should be deleted. */
    {
        auto fPath = GetCWDString() + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#APPDATA#", true) +
                     L"\\data.txt.$APPBOX_DELETE$";
        ASSERT_FALSE(std::filesystem::exists(fPath));
    }

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
