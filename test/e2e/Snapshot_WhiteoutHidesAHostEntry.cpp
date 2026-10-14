#include "probe/ListDir.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <algorithm>
#include <chrono>
#include <CLI/Encoding.hpp>
#include <string>

typedef appbox::test::CommonFixture E2E_Snapshot;
using namespace appbox::test;

namespace
{

/**
 * @brief Generate a unique name fragment for the folder of a case.
 * @return The unique fragment.
 */
std::wstring UniqueFragment()
{
    static unsigned counter = 0;
    const auto      ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_wstring(ticks) + L"-" + std::to_wstring(++counter);
}

} // namespace

/**
 * Condition:
 * 1. The host holds the folder `#USERPROFILE#\appbox-e2e-whiteout-<fragment>` with
 *    the files `old.dll` and `keep.dll`.
 * 2. The layer `#USERPROFILE#` of the archive carries `packed.txt` and the
 *    whiteout marker of `old.dll`, which is the shape the packer writes for an
 *    entry a captured snapshot reports as removed.
 * 3. List the folder of the host.
 *
 * Expected:
 * 1. `old.dll` is invisible for the packaged application.
 * 2. `keep.dll` of the host and `packed.txt` of the layer are visible.
 * 3. The host folder is untouched.
 */
TEST_F(E2E_Snapshot, WhiteoutHidesAHostEntry)
{
    const std::wstring name = L"appbox-e2e-whiteout-" + UniqueFragment();

    appbox::test::RealFsFolder folder(L"#USERPROFILE#", name);
    ASSERT_TRUE(folder.WriteFile(L"old.dll", "OLD"));
    ASSERT_TRUE(folder.WriteFile(L"keep.dll", "KEEP"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#\\" + name, {
                FsFile(L"packed.txt", "PACKED"),
                FsFile(L"old.dll.$APPBOX_DELETE$", "")
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    ProtocolListDir::Req req;
    req.path = CLI::narrow(GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + name);
    req.method = ProtocolListDir::Req::Method::Std;

    ProtocolListDir::Rsp rsp;
    ProbeListDir.Call(req, GetCWD(), config).get_to(rsp);

    const auto listed = [&rsp](const std::string& entry) {
        return std::any_of(rsp.entries.begin(), rsp.entries.end(),
                           [&entry](const auto& item) { return item.name == entry; });
    };

    /* The marker of the layer hides the entry of the host and nothing else. */
    EXPECT_FALSE(listed("old.dll"));
    EXPECT_TRUE(listed("keep.dll"));
    EXPECT_TRUE(listed("packed.txt"));

    /* The host folder keeps its content. */
    EXPECT_TRUE(folder.FileExists(L"old.dll"));
    EXPECT_TRUE(folder.FileExists(L"keep.dll"));

    ASSERT_TRUE(tree.Verify());
}
