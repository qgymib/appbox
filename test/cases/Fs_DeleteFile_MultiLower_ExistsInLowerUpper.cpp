#include "builder/FsBuilder.hpp"
#include "probe/DeleteFileW.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/KnownFolder.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

/**
 * Condition:
 * 1. File exists in upper and lower fs
 * 2. Try to delete file.
 *
 * Expected:
 * 1. Delete file success.
 * 2. File not exists in upper fs.
 * 3. Whiteout file exists in upper fs.
 */
TEST_F(Fs, DeleteFile_MultiLower_ExistsInLowerUpper)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {
            FsDir(GetKnownFolderPath(L"%APPDATA%", true), {
                FsFile(L"data.txt", "hello")
            })
        }),
        FsDir(L"Lower1", {
            FsDir(L"%APPDATA%", {
                FsFile(L"data.txt", "hello1")
            })
        }),
        FsDir(L"Lower2", {
            FsDir(L"%APPDATA%", {
                FsFile(L"data.txt", "hello2")
            })
        })
    });
    /* clang-format on */

    /* Build filesystem tree. */
    auto config = tree.Build();

    /* Delete file. */
    {
        ProtocolDeleteFileW::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"%APPDATA%", false) + L"\\data.txt");

        auto rsp = ProbeDeleteFileW.Call(req, GetCWD(), config).get<ProtocolDeleteFileW::Rsp>();
        ASSERT_EQ(rsp.code, 0);
    }

    /* File should not exist in upper fs. */
    {
        auto fPath = GetCWDString() + L"\\Upper\\" + GetKnownFolderPath(L"%APPDATA%", true) + L"\\data.txt";
        ASSERT_FALSE(std::filesystem::exists(fPath));
    }

    /* Whiteout file should exist in upper fs. */
    {
        auto fPath = GetCWDString() + L"\\Upper\\" + GetKnownFolderPath(L"%APPDATA%", true) +
                     L"\\data.txt.$APPBOX_DELETE$";
        ASSERT_TRUE(std::filesystem::exists(fPath));
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
