#include "probe/ListDir.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include <algorithm>
#include <CLI/Encoding.hpp>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

/**
 * Condition:
 * 1. The directory of the host exists and the lower layer holds a file below
 *    it.
 *
 * Expected:
 * 1. The file exists in the merged view.
 * 2. The name appears exactly once, so a lower layer entry is not emitted
 *    twice.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, ListDir_LowerLayer)
{
    const std::string  fName = "Fs.ListDir_LowerLayer.txt";
    const std::wstring wName = CLI::widen(fName);

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsFile(wName, "hello1")
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
        req.path = CLI::narrow(GetKnownFolderPath(L"#USERPROFILE#", false));
        req.method = ProtocolListDir::Req::Method::Std;
        ProbeListDir.Call(req, GetCWD(), config).get_to(rsp);

        /* Target file should be found. */
        auto it = std::find_if(rsp.entries.begin(), rsp.entries.end(),
                               [&fName](const auto& e) { return e.file && e.name == fName; });
        ASSERT_NE(it, rsp.entries.end());
    }

    /* Entry count should larger than 1, because nativate filesystem should have files in #USERPROFILE# */
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

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
