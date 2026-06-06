#include "probe/ListDir.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/KnownFolder.hpp"
#include <algorithm>
#include <CLI/Encoding.hpp>

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

static size_t SearchInRsp(const ProtocolListDir::Rsp& rsp, const std::wstring& name)
{
    size_t count = 0;
    auto   uName = CLI::narrow(name);

    for (auto& e : rsp.entries)
    {
        if (e.name == uName)
        {
            count++;
        }
    }
    return count;
}

TEST_F(Fs, ListDir_MultiLower_ExistsInLower_WhiteoutInUpper)
{
    const std::string  fName = "Fs.ListDir_MultiLower_ExistsInLower_WhiteoutInUpper.txt";
    const std::wstring wName = CLI::widen(fName);
    const std::wstring wName2 = L"Fs.ListDir_MultiLower_ExistsInLower_WhiteoutInUpper.txt2";

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {
            FsDir(GetKnownFolderPath(L"%APPDATA%", true), {
                FsFile(wName + L".$APPBOX_DELETE$", "")
            })
        }),
        FsDir(L"Lower1", {
            FsDir(L"%APPDATA%", {
                FsFile(wName, "hello1")
            })
        }),
        FsDir(L"Lower2", {
            FsDir(L"%APPDATA%", {
                FsFile(wName2, "hello2")
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

        /* Target file should not be found. */
        auto it = std::find_if(rsp.entries.begin(), rsp.entries.end(),
                               [&fName](const auto& e) { return e.file && e.name == fName; });
        ASSERT_EQ(it, rsp.entries.end());
    }

    ASSERT_EQ(SearchInRsp(rsp, wName), 0);
    ASSERT_EQ(SearchInRsp(rsp, wName2), 1);

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
