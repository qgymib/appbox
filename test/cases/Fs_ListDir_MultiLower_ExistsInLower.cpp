#include "probe/ListDir.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/KnownFolder.hpp"
#include <algorithm>
#include <CLI/Encoding.hpp>

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

/**
 * Condition:
 * 1. Dir exists in lower fs.
 * 2. Different file content in lower fs.
 *
 * Expected:
 * 1. File exists in mapped view.
 * 2. File only show once.
 */
TEST_F(Fs, ListDir_MultiLower_ExistsInLower)
{
    const std::string  fName = "Fs.ListDir_MultiLower_ExistsInLower.txt";
    const std::wstring wName = CLI::widen(fName);

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {
            FsDir(L"filesystem\\%APPDATA%", {
                FsFile(wName, "hello1")
            })
        }),
        FsDir(L"Lower2", {
            FsDir(L"filesystem\\%APPDATA%", {
                FsFile(wName, "hello2")
            })
        })
    });
    /* clang-format on */

    /* Build filesystem tree. */
    auto config = tree.Build();

    ProtocolListDir::Rsp rsp;
    {
        /* List directory entries. */
        ProtocolListDir::Req req;
        req.path = CLI::narrow(GetKnownFolderPath(L"%APPDATA%", false));
        req.method = ProtocolListDir::Req::Method::Std;
        ProbeListDir.Call(req, GetCWD(), config).get_to(rsp);

        /* Target file should be found. */
        auto it = std::find_if(rsp.entries.begin(), rsp.entries.end(),
                               [&fName](const auto& e) { return e.file && e.name == fName; });
        ASSERT_NE(it, rsp.entries.end());
    }

    /* Entry count should larger than 1, because nativate filesystem should have files in %APPDATA% */
    ASSERT_GT(rsp.entries.size(), 1);

    /* Verify file number */
    {
        size_t fCount = 0;
        for (const auto& e : rsp.entries)
        {
            if (e.name == fName)
            {
                fCount++;
            }
        }
        ASSERT_EQ(fCount, 1);
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
