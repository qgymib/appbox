#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/KnownFolder.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

/**
 * Condition:
 * 1. File exists in lower fs but whiteout in upper fs.
 * 2. Try to read file.
 *
 * Expected:
 * 1. Cannot read file.
 */
TEST_F(Fs, ReadFile_MultiLower_WhiteoutInUpper)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {
            FsDir(GetKnownFolderPath(L"%APPDATA%", true), {
                FsFile(L"data.txt.$APPBOX_DELETE$", "")
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

    /* Try to read file */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"%APPDATA%", false) + L"\\data.txt");

        auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_NE(rsp.code, 0);
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
