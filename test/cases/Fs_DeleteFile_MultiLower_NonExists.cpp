#include "probe/DeleteFileW.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/KnownFolder.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

/**
 * Condition:
 * 1. File not exists in any fs.
 * 2. Try to delete file.
 *
 * Expected:
 * 1. Delete file failed.
 */
TEST_F(Fs, DeleteFile_MultiLower_NonExists)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {
            FsDir(L"%APPDATA%", {
                FsFile(L"data1.txt", "hello1")
            })
        }),
        FsDir(L"Lower2", {
            FsDir(L"%APPDATA%", {
                FsFile(L"data2.txt", "hello2")
            })
        })
    });
    /* clang-format on */

    /* Build filesystem tree. */
    auto config = tree.Build();

    /* Delete file should fail. */
    {
        ProtocolDeleteFileW::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"%APPDATA%", false) + L"\\data.txt");

        auto rsp = ProbeDeleteFileW.Call(req, GetCWD(), config).get<ProtocolDeleteFileW::Rsp>();
        ASSERT_NE(rsp.code, 0);
    }

    /* No whiteout file in upper fs */
    {
        auto fPath = GetCWDString() + L"\\Upper\\" + GetKnownFolderPath(L"%APPDATA%", true) +
                     L"\\data.txt.$APPBOX_DELETE$";
        ASSERT_FALSE(std::filesystem::exists(fPath));
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
