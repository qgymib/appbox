#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "utils/WriteFileFull.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <filesystem>
#include <set>
#include <string>

typedef appbox::test::CommonFixture E2E_Patch;
using namespace appbox::test;

namespace
{

/** Name of the file of the case below the user profile. */
constexpr wchar_t kName[] = L"AppBoxTest_Patch.NoPackage.txt";

} // namespace

/**
 * Condition:
 * 1. The user created the patch directory, but it holds a file which is not a
 *    package instead of a package.
 * 2. The sandboxed process reads the file of the archive.
 *
 * Expected:
 * 1. The read returns the content of the archive, because a file without the
 *    extension of a package is not applied.
 * 2. The cache directory below the state directory carries the two sandbox
 *    modules the launcher releases at every start and nothing else, because the
 *    patch directory holds no package to extract.
 */
TEST_F(E2E_Patch, NoPackageLeavesNoCacheEntry)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsFile(kName, "app")
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));
    ASSERT_EQ(WriteFileFull((patch_dir / L"readme.txt").wstring(), std::string("not a package")),
              static_cast<DWORD>(ERROR_SUCCESS));

    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kName);

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "app");
    }

    /*
     * The launcher released the modules of the run, so the cache directory
     * exists, but no package was extracted into it.
     */
    const auto cache = GetCWD() / appbox::layout::kStateDirNameW / appbox::layout::kCacheDirNameW;
    ASSERT_TRUE(std::filesystem::is_directory(cache));

    std::set<std::wstring> entries;
    for (const auto& entry : std::filesystem::directory_iterator(cache))
    {
        entries.insert(entry.path().filename().wstring());
    }

    const std::set<std::wstring> released = { appbox::layout::kSandbox32DllNameW, appbox::layout::kSandbox64DllNameW };
    EXPECT_EQ(entries, released);

    /* The resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
