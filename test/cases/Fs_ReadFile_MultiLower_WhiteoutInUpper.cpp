#include "builder/FsBuilder.hpp"
#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
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
    auto tree = FsRoot::Make(GetCWD(), {
        FsDir::Make(L"Upper", {
            FsDir::Make(GetKnownFolderPath(L"%APPDATA%", true), {
                FsFile::Make(L"data.txt.$APPBOX_DELETE$", "")
            })
        }),
        FsDir::Make(L"Lower1", {
            FsDir::Make(L"%APPDATA%", {
                FsFile::Make(L"data.txt", "hello1")
            })
        }),
        FsDir::Make(L"Lower2", {
            FsDir::Make(L"%APPDATA%", {
                FsFile::Make(L"data.txt", "hello2")
            })
        })
    });
    /* clang-format on */

    /* Build filesystem tree. */
    auto config = tree->Build();

    /* Try to read file */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"%APPDATA%", false) + L"\\data.txt");

        auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_NE(rsp.code, 0);
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree->Verify());
}
