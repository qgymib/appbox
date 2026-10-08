#include "probe/CreateDirectoryW.hpp"
#include "probe/CreateFileW.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_DefaultMergeCreate";

/**
 * @brief Get the path of the folder of the case inside the view.
 * @return Path of the folder.
 */
std::wstring FolderPath()
{
    return GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
}

/**
 * @brief Get the path of the folder of the case inside the overlay.
 * @param[in] cwd Working directory of the case.
 * @return Path of the folder in the overlay.
 */
std::wstring FolderInOverlay(const std::wstring& cwd)
{
    return cwd + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) + L"\\" + kFolderName;
}

} // namespace

/**
 * Condition:
 * 1. No isolation file of the case lists an entry for the folder, so the mode
 *    of every path below it is the default of the view, which is `Merge`.
 * 2. The host holds the folder of the case; the sandboxed process creates a
 *    directory and a file below it.
 *
 * Expected:
 * 1. The default reaches the modifications: the directory and the file are
 *    created in the host filesystem, because the host holds neither of them
 *    while no layer holds them either.
 * 2. The overlay of the sandbox holds neither of them, so the state of the
 *    sandbox is empty and the resources of the application stay untouched.
 */
TEST_F(E2E_Fs, DefaultMerge_CreatesTheEntriesInTheHost)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {})
    });
    /* clang-format on */

    auto config = tree.Build();

    /* The directory of the case is created in the host filesystem. */
    {
        ProtocolCreateDirectoryW::Req req;
        req.PathName = appbox::WideToUTF8(FolderPath() + L"\\created");

        const auto rsp = ProbeCreateDirectoryW.Call(req, GetCWD(), config).get<ProtocolCreateDirectoryW::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    /* A file which no layer holds is created in the host filesystem as well. */
    {
        ProtocolCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\created\\data.txt");
        req.dwDesiredAccess = GENERIC_WRITE;
        req.dwCreationDisposition = CREATE_NEW;

        const auto rsp = ProbeCreateFileW.Call(req, GetCWD(), config).get<ProtocolCreateFileW::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    /* The host gained both entries, the overlay holds neither of them. */
    EXPECT_TRUE(host.FileExists(L"created"));
    EXPECT_TRUE(host.FileExists(L"created\\data.txt"));
    EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\created"));

    ASSERT_TRUE(tree.Verify());
}
