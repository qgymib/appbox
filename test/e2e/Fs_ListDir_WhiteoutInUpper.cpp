#include "probe/ListDir.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include <algorithm>
#include <CLI/Encoding.hpp>

typedef appbox::test::CommonFixture E2E_Fs;
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

/**
 * Condition:
 * 1. The lower layer holds two files.
 * 2. The state of the sandbox carries a whiteout marker for one of them.
 *
 * Expected:
 * 1. The name of the marker is hidden, even though the lower layer holds it.
 * 2. The other name of the lower layer stays visible.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, ListDir_WhiteoutInUpper)
{
    const std::string  fName = "Fs.ListDir_WhiteoutInUpper.txt";
    const std::wstring wName = CLI::widen(fName);
    const std::wstring wName2 = L"Fs.ListDir_WhiteoutInUpper.txt2";

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {
            FsDir(L"filesystem\\" + GetKnownFolderPath(L"#APPDATA#", true), {
                FsFile(wName + L".$APPBOX_DELETE$", "")
            })
        }),
        FsDir(L"app", {
            FsDir(L"filesystem\\#APPDATA#", {
                FsFile(wName, "hello1"),
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
        req.path = CLI::narrow(GetKnownFolderPath(L"#APPDATA#", false));
        req.method = ProtocolListDir::Req::Method::Std;
        ProbeListDir.Call(req, GetCWD(), config).get_to(rsp);

        /* Target file should not be found. */
        auto it = std::find_if(rsp.entries.begin(), rsp.entries.end(),
                               [&fName](const auto& e) { return e.file && e.name == fName; });
        ASSERT_EQ(it, rsp.entries.end());
    }

    ASSERT_EQ(SearchInRsp(rsp, wName), 0);
    ASSERT_EQ(SearchInRsp(rsp, wName2), 1);

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
