#include "probe/DeleteFileW.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/KnownFolder.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

TEST_F(Fs, DeleteFile_WhiteoutInLower_ExistsInUpper)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {
            FsDir(L"filesystem\\" + GetKnownFolderPath(L"%APPDATA%", true), {
                FsFile(L"data.txt", "hello")
            })
        }),
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

    /* Delete file should success. */
    {
        ProtocolDeleteFileW::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"%APPDATA%", false) + L"\\data.txt");

        auto rsp = ProbeDeleteFileW.Call(req, GetCWD(), config).get<ProtocolDeleteFileW::Rsp>();
        ASSERT_EQ(rsp.code, 0);
    }

    /* No whiteout file in upper fs */
    {
        auto fPath = GetCWDString() + L"\\Upper\\filesystem\\" + GetKnownFolderPath(L"%APPDATA%", true) +
                     L"\\data.txt.$APPBOX_DELETE$";
        ASSERT_FALSE(std::filesystem::exists(fPath));
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
