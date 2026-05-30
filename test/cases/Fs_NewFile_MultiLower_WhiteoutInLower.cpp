#include "probe/CreateFileW.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/KnownFolder.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

/**
 * Condition:
 * 1. File exists in lower fs but also whiteout in lower fs.
 * 2. Try to create file.
 *
 * Expected:
 * 1. New file is created in upper fs.
 */
TEST_F(Fs, NewFile_MultiLower_WhiteoutInLower)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {
            FsDir(L"filesystem\\%APPDATA%", {
                FsFile(L"data.txt.$APPBOX_DELETE$", "")
            })
        }),
        FsDir(L"Lower2", {
            FsDir(L"filesystem\\%APPDATA%", {
                FsFile(L"data.txt", "hello2")
            })
        })
    });
    /* clang-format on */

    /* Build filesystem tree. */
    auto config = tree.Build();

    /* Create file in upper should success. */
    {
        ProtocolCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"%APPDATA%", false) + L"\\data.txt");
        req.dwDesiredAccess = GENERIC_WRITE;
        req.dwCreationDisposition = CREATE_NEW;
        auto rsp = ProbeCreateFileW.Call(req, GetCWD(), config).get<ProtocolCreateFileW::Rsp>();
        ASSERT_EQ(rsp.code, 0);
    }

    /* Target file should be created. */
    {
        auto fPath = GetCWDString() + L"\\Upper\\filesystem\\" + GetKnownFolderPath(L"%APPDATA%", true) + L"\\data.txt";
        ASSERT_TRUE(std::filesystem::exists(fPath));
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
