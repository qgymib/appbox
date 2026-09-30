#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/TestKnownFolder.hpp"
#include "utils/WriteFileFull.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Patch;
using namespace appbox::test;

namespace
{

/** Name of the file of the case below the user profile. */
constexpr wchar_t kName[] = L"AppBoxTest_Patch.Cache.txt";

/** Number of hexadecimal characters the recorded digest has. */
constexpr std::size_t kDigestTextSize = 32;

/**
 * @brief Write a patch package which carries the file of the case.
 * @param[in] path Path of the package.
 * @param[in] content Content the package carries for the file.
 * @return true on success.
 */
bool WritePackage(const std::filesystem::path& path, const char* content)
{
    return WritePatchPackage(path, {
                                       { std::wstring(L"#USERPROFILE#\\") + kName, content }
    });
}

/**
 * @brief Read the file of the case inside the sandbox.
 * @param[in] folder Path of the user profile of the host.
 * @param[in] cwd Working directory of the case.
 * @param[in] config Configuration of the case.
 * @return The content the view holds for the file.
 */
std::string ReadPatchFile(const std::wstring& folder, const std::filesystem::path& cwd, appbox::LauncherConfig& config)
{
    ProtocolReadFileFull::Req req;
    req.FileName = appbox::WideToUTF8(folder + L"\\" + kName);

    const auto rsp = ProbeReadFileFull.Call(req, cwd, config).get<ProtocolReadFileFull::Rsp>();
    EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    return rsp.data;
}

} // namespace

/**
 * Condition:
 * 1. `00-foo.zip` carries the file of the case with the content `one`.
 * 2. The sandbox runs four times: with the package as it is, after a file was
 *    placed inside its cache entry, after the package was replaced with one
 *    which carries the content `two` and after the cache directory was
 *    deleted.
 *
 * Expected:
 * 1. The first run reads `one` and records the digest of the package in its
 *    cache entry, so the entry describes the package it holds.
 * 2. The second run reads `one` and keeps the file which was placed inside the
 *    cache entry, so the extraction of an unchanged package is reused.
 * 3. The third run reads `two` and drops the file, so a package whose digest
 *    changed is extracted again and takes effect.
 * 4. The fourth run reads `two` as well and extracts the package again, so
 *    deleting the cache directory costs the extraction of the next run and
 *    never changes the view of the sandbox.
 */
TEST_F(E2E_Patch, CacheIsReusedAndRefreshed)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {})
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));
    const auto package = patch_dir / L"00-foo.zip";
    ASSERT_TRUE(WritePackage(package, "one"));

    const auto folder = GetKnownFolderPath(L"#USERPROFILE#", false);
    const auto cache_entry = GetCWD() / appbox::layout::kCacheDirNameW / L"00-foo";
    const auto digest_file = cache_entry / appbox::layout::kPatchDigestFileNameW;
    const auto marker = cache_entry / L"marker.txt";

    /* The first run extracts the package and records its digest. */
    EXPECT_EQ(ReadPatchFile(folder, GetCWD(), config), "one");
    ASSERT_TRUE(std::filesystem::exists(digest_file));
    {
        std::string digest;
        ASSERT_EQ(ReadFileFull(digest_file.wstring(), digest), static_cast<DWORD>(ERROR_SUCCESS));
        while (!digest.empty() && (digest.back() == '\n' || digest.back() == '\r'))
        {
            digest.pop_back();
        }

        EXPECT_EQ(digest.size(), kDigestTextSize);
        EXPECT_EQ(digest.find_first_not_of("0123456789abcdef"), std::string::npos) << digest;
    }
    EXPECT_TRUE(std::filesystem::exists(cache_entry / appbox::layout::kFilesystemDirNameW / L"#USERPROFILE#" / kName));

    /* A run whose package did not change reuses the extraction. */
    ASSERT_EQ(WriteFileFull(marker.wstring(), std::string("marker")), static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(ReadPatchFile(folder, GetCWD(), config), "one");
    EXPECT_TRUE(std::filesystem::exists(marker));

    /* A replaced package is extracted again and takes effect. */
    ASSERT_TRUE(WritePackage(package, "two"));
    EXPECT_EQ(ReadPatchFile(folder, GetCWD(), config), "two");
    EXPECT_FALSE(std::filesystem::exists(marker));

    /* A run whose cache was deleted extracts the package again. */
    {
        std::error_code ec;
        ASSERT_GT(std::filesystem::remove_all(GetCWD() / appbox::layout::kCacheDirNameW, ec), 0u);
        ASSERT_FALSE(ec);
    }
    EXPECT_EQ(ReadPatchFile(folder, GetCWD(), config), "two");
    EXPECT_TRUE(std::filesystem::exists(digest_file));

    /* The resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
