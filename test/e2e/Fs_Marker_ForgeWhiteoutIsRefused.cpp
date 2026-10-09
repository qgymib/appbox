#include "probe/CreateFileW.hpp"
#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_MarkerForge";

/** Name of the entry the case tries to hide. */
constexpr wchar_t kVictimName[] = L"victim.txt";

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

/**
 * @brief Ask the sandboxed process to create a name.
 * @param[in] path Path to create.
 * @param[in] cwd Working directory of the case.
 * @param[in] config Configuration of the case.
 * @return The error code of the creation.
 */
DWORD CreateNew(const std::wstring& path, const std::filesystem::path& cwd, appbox::LauncherConfig& config)
{
    ProtocolCreateFileW::Req req;
    req.FileName = appbox::WideToUTF8(path);
    req.dwDesiredAccess = GENERIC_WRITE;
    req.dwCreationDisposition = CREATE_NEW;

    return ProbeCreateFileW.Call(req, cwd, config).get<ProtocolCreateFileW::Rsp>().code;
}

} // namespace

/**
 * Condition:
 * 1. The lower layer holds the folder of the case with `victim.txt`, and the
 *    folder isolates with `Write Copy`, so a creation inside it would land in
 *    the overlay.
 * 2. The sandboxed process tries to create the whiteout marker of `victim.txt`
 *    and the marker of one of its streams itself.
 *
 * Expected:
 * 1. Both creations are refused with `Invalid Name`, because the names of the
 *    markers are reserved by the view.
 * 2. The entry the marker would hide stays visible, and the overlay records
 *    neither the marker nor the entry.
 */
TEST_F(E2E_Fs, Marker_ForgeWhiteoutIsRefused)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"victim.txt", "packed")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
                       appbox::FilesystemIsolation::WriteCopy }
    }));

    /* The marker of an entry, and the marker of a stream of that entry. */
    EXPECT_EQ(CreateNew(FolderPath() + L"\\" + kVictimName + L".$APPBOX_DELETE$", GetCWD(), config),
              static_cast<DWORD>(ERROR_INVALID_NAME));
    EXPECT_EQ(CreateNew(FolderPath() + L"\\" + kVictimName + L":stream.$APPBOX_DELETE$", GetCWD(), config),
              static_cast<DWORD>(ERROR_INVALID_NAME));

    /* The entry the marker would hide stays visible. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\" + kVictimName);

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "packed");
    }

    /* The overlay recorded neither the marker nor a copy of the entry. */
    {
        const auto overlay = FolderInOverlay(GetCWDString());
        EXPECT_FALSE(std::filesystem::exists(overlay + L"\\" + kVictimName + L".$APPBOX_DELETE$"));
        EXPECT_FALSE(std::filesystem::exists(overlay + L"\\" + kVictimName));
    }

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
