#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "utils/WriteFileFull.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <filesystem>
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
 * 2. The cache directory was not created, because the launcher only creates it
 *    while the patch directory holds a package.
 */
TEST_F(E2E_Patch, NoPackageKeepsTheCacheEmpty)
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

    EXPECT_FALSE(std::filesystem::exists(GetCWD() / appbox::layout::kCacheDirNameW));

    /* The resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
