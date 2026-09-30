#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>
#include <nlohmann/json.hpp>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_Malformed";

/** A version number no sandbox of this build knows. */
constexpr int kUnknownVersion = 99;

/**
 * @brief Read a file inside the sandbox.
 * @param[in] file Path of the file in the view.
 * @param[in] cwd Working directory of the case.
 * @param[in] config Launcher configuration of the sandbox.
 * @return The response of the probe.
 */
ProtocolReadFileFull::Rsp ReadFile(const std::wstring& file, const std::filesystem::path& cwd,
                                   const appbox::LauncherConfig& config)
{
    ProtocolReadFileFull::Req req;
    req.FileName = appbox::WideToUTF8(file);
    return ProbeReadFileFull.Call(req, cwd, config).get<ProtocolReadFileFull::Rsp>();
}

} // namespace

/**
 * Condition:
 * 1. The folder exists in a lower layer and in the host.
 * 2. The isolation file of the overlay cannot be used: it is not a JSON
 *    document in the first half of the case and it carries a version the
 *    sandbox does not know in the second one. The document of the last half is
 *    the valid one which hides the host folder.
 *
 * Expected:
 * 1. A document which cannot be read is ignored: the sandbox behaves like one
 *    without an isolation file, so the host file stays visible while the
 *    content of the virtual filesystem is visible as well.
 * 2. The same entry hides the host file as soon as the document is readable,
 *    which is what makes the two runs above a check of the fallback instead of
 *    a check of the default mode.
 */
TEST_F(E2E_Fs, MalformedIsolationFile_FallsBack)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"host.txt", "host"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"packed.txt", "packed")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto folder = GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
    const auto host_file = folder + L"\\host.txt";
    const auto packed_file = folder + L"\\packed.txt";

    /* A document which is not JSON at all is ignored. */
    ASSERT_TRUE(WriteRawFsIsolationFile(GetCWD(), "{ \"version\": 1, \"entries\": ["));
    {
        const auto rsp = ReadFile(host_file, GetCWD(), config);
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "host");
    }

    /* A document of a version the sandbox does not know is ignored as well. */
    {
        nlohmann::json document;
        document[appbox::filesystem_isolation::kVersionKey] = kUnknownVersion;
        document[appbox::filesystem_isolation::kEntriesKey] = nlohmann::json::array();
        ASSERT_TRUE(WriteRawFsIsolationFile(GetCWD(), document.dump(2)));
    }
    {
        const auto rsp = ReadFile(host_file, GetCWD(), config);
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "host");
    }

    /* The same entry of a readable document hides the host file, so the two
     * runs above really are the fallback of a refused document. */
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
                       appbox::FilesystemIsolation::Full }
    }));
    {
        const auto rsp = ReadFile(host_file, GetCWD(), config);
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The content of the virtual filesystem is visible in every one of them. */
    {
        const auto rsp = ReadFile(packed_file, GetCWD(), config);
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "packed");
    }

    ASSERT_TRUE(tree.Verify());
}
