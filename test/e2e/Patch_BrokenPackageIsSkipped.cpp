#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "utils/WriteFileFull.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Patch;
using namespace appbox::test;

namespace
{

/** Name of the file of the case below the user profile. */
constexpr wchar_t kName[] = L"AppBoxTest_Patch.Broken.txt";

} // namespace

/**
 * Condition:
 * 1. The resources of the archive carry the file of the case with the content
 *    `app`.
 * 2. `00-bad.zip` is not an archive at all and `01-good.zip` carries the file
 *    with the content `good`.
 * 3. The sandboxed process reads the file.
 *
 * Expected:
 * 1. The read returns `good`, so the package which cannot be read is skipped
 *    while the package after it is applied and the run succeeds.
 * 2. The package which cannot be read left no cache entry behind, because a
 *    cache entry only exists for a complete extraction.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Patch, BrokenPackageIsSkipped)
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

    ASSERT_EQ(WriteFileFull((patch_dir / L"00-bad.zip").wstring(), std::string("this is not a zip archive")),
              static_cast<DWORD>(ERROR_SUCCESS));
    ASSERT_TRUE(WritePatchPackage(patch_dir / L"01-good.zip", {
                                                                  { std::wstring(L"#USERPROFILE#\\") + kName, "good" },
    }));

    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kName);

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "good");
    }

    /* The broken package left no cache entry behind, the good one did. */
    const auto cache = GetCWD() / appbox::layout::kCacheDirNameW;
    EXPECT_FALSE(std::filesystem::exists(cache / L"00-bad"));
    EXPECT_TRUE(std::filesystem::exists(cache / L"01-good" / appbox::layout::kPatchDigestFileNameW));

    /* The resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
