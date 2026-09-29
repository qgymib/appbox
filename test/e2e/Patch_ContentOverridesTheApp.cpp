#include "probe/ListDir.hpp"
#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Patch;
using namespace appbox::test;

namespace
{

/** Name of the file every layer of the case carries. */
constexpr wchar_t kSharedName[] = L"AppBoxTest_Patch.Both.txt";

/** Name of a file only the resources of the archive carry. */
constexpr wchar_t kAppName[] = L"AppBoxTest_Patch.App.txt";

/** Name of a file only the first patch package carries. */
constexpr wchar_t kFirstPackageName[] = L"AppBoxTest_Patch.First.txt";

/**
 * @brief Whether a listing holds a file of a given name.
 * @param[in] rsp Listing of a directory.
 * @param[in] name Name of the file.
 * @return true when the file is part of the listing.
 */
bool HoldsFile(const ProtocolListDir::Rsp& rsp, const std::wstring& name)
{
    const auto utf8 = appbox::WideToUTF8(name);
    return std::any_of(rsp.entries.begin(), rsp.entries.end(),
                       [&utf8](const ProtocolListDir::Rsp::Entry& entry) { return entry.file && entry.name == utf8; });
}

/**
 * @brief Build the virtual path of a file of the case.
 * @param[in] name Name of the file below the user profile.
 * @return The path of the file in the virtual filesystem.
 */
std::wstring VirtualPathOf(const wchar_t* name)
{
    return std::wstring(L"#USERPROFILE#\\") + name;
}

} // namespace

/**
 * Condition:
 * 1. The resources of the archive carry a file the two patch packages carry as
 *    well, a file only the archive carries and no patch package carries.
 * 2. `00-foo.zip` carries the shared file with a content of its own and a file
 *    only that package carries; `01-bar.zip` carries the shared file with a
 *    third content.
 * 3. The sandboxed process reads the shared file and lists the folder.
 *
 * Expected:
 * 1. The shared file holds the content of `01-bar.zip`, so a package overrides
 *    the resources of the archive and the later package overrides the earlier
 *    one.
 * 2. The file of the archive and the file of the first package stay readable,
 *    because a package overrides the resources it carries and not the whole
 *    layer below it.
 * 3. Every package was extracted into the cache of the run.
 * 4. The resources of the application are untouched.
 */
TEST_F(E2E_Patch, ContentOverridesTheApp)
{
    const std::wstring folder = GetKnownFolderPath(L"#USERPROFILE#", false);

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsFile(kSharedName, "app"),
                FsFile(kAppName, "app")
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));
    ASSERT_TRUE(WritePatchPackage(patch_dir / L"00-foo.zip", {
                                                                 { VirtualPathOf(kSharedName),       "foo" },
                                                                 { VirtualPathOf(kFirstPackageName), "foo" },
    }));
    ASSERT_TRUE(WritePatchPackage(patch_dir / L"01-bar.zip", {
                                                                 { VirtualPathOf(kSharedName), "bar" },
    }));

    /* The file every layer carries is the file of the last package. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\" + kSharedName);

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "bar");
    }

    /* The layers below the last package stay in place for the other resources. */
    {
        ProtocolListDir::Req req;
        req.path = appbox::WideToUTF8(folder);
        req.method = ProtocolListDir::Req::Method::Std;

        const auto rsp = ProbeListDir.Call(req, GetCWD(), config).get<ProtocolListDir::Rsp>();
        EXPECT_TRUE(HoldsFile(rsp, kSharedName));
        EXPECT_TRUE(HoldsFile(rsp, kAppName));
        EXPECT_TRUE(HoldsFile(rsp, kFirstPackageName));
    }

    /* Every package was extracted into its cache entry. */
    {
        const auto entry = GetCWD() / appbox::layout::kCacheDirNameW / L"00-foo";
        EXPECT_TRUE(std::filesystem::exists(entry / appbox::layout::kPatchDigestFileNameW));
        EXPECT_TRUE(
            std::filesystem::exists(entry / appbox::layout::kFilesystemDirNameW / L"#USERPROFILE#" / kSharedName));
    }

    /* The resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
