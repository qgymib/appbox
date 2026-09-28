#include "probe/DeleteFileW.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

/**
 * Condition:
 * 1. File exists in the state of the sandbox only.
 * 2. Try to delete file.
 *
 * Expected:
 * 1. Delete file success.
 * 2. No whiteout file in the state of the sandbox, because no lower layer
 *    holds the name.
 */
TEST_F(E2E_Fs, DeleteFile_UpperOnly)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {
            FsDir(L"filesystem\\" + GetKnownFolderPath(L"#APPDATA#", true), {
                FsFile(L"data.txt", "hello")
            })
        }),
        FsDir(L"app", {})
    });
    /* clang-format on */

    /* Build filesystem tree. */
    auto config = tree.Build();

    /* Delete file. */
    {
        ProtocolDeleteFileW::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"#APPDATA#", false) + L"\\data.txt");

        auto rsp = ProbeDeleteFileW.Call(req, GetCWD(), config).get<ProtocolDeleteFileW::Rsp>();
        ASSERT_EQ(rsp.code, 0);
    }

    /* File should not exist in the state of the sandbox. */
    {
        auto fPath = GetCWDString() + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#APPDATA#", true) + L"\\data.txt";
        ASSERT_FALSE(std::filesystem::exists(fPath));
    }

    /* Whiteout file should not exist in the state of the sandbox. */
    {
        auto fPath = GetCWDString() + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#APPDATA#", true) +
                     L"\\data.txt.$APPBOX_DELETE$";
        ASSERT_FALSE(std::filesystem::exists(fPath));
    }

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
