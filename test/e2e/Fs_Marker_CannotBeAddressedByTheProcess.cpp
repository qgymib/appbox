#include "probe/CreateFileW.hpp"
#include "probe/DeleteFileW.hpp"
#include "probe/QueryAttributes.hpp"
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
constexpr wchar_t kFolderName[] = L"AppBoxTest_MarkerAddress";

/** Name of the entry the whiteout marker of the case hides. */
constexpr wchar_t kVictimName[] = L"data.txt";

/** Name of the whiteout marker of `kVictimName`. */
constexpr wchar_t kMarkerName[] = L"data.txt.$APPBOX_DELETE$";

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
 * 1. The lower layer holds `data.txt` and the overlay records the delete of
 *    that entry with a whiteout marker, so the view reports the entry as gone.
 * 2. The folder of the case isolates with `Write Copy`.
 * 3. The sandboxed process opens, queries and deletes the name of the marker.
 *
 * Expected:
 * 1. Every call reports `File Not Found`: the name of a marker is reserved, so
 *    it names no entry of the view.
 * 2. The marker survives, which keeps the entry it hides hidden.
 */
TEST_F(E2E_Fs, Marker_CannotBeAddressedByTheProcess)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {
            FsDir(L"filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true), {
                FsDir(kFolderName, {
                    FsFile(kMarkerName, "")
                })
            })
        }),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(kVictimName, "packed")
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

    const std::wstring marker = FolderPath() + L"\\" + kMarkerName;

    /* The marker cannot be opened. */
    {
        ProtocolCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(marker);
        req.dwDesiredAccess = GENERIC_READ;
        req.dwCreationDisposition = OPEN_EXISTING;

        const auto rsp = ProbeCreateFileW.Call(req, GetCWD(), config).get<ProtocolCreateFileW::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The marker cannot be queried. */
    {
        ProtocolQueryAttributes::Req req;
        req.FileName = appbox::WideToUTF8(marker);

        const auto rsp = ProbeQueryAttributes.Call(req, GetCWD(), config).get<ProtocolQueryAttributes::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
        EXPECT_EQ(rsp.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    }

    /* The marker cannot be removed, which would unhide the entry. */
    {
        ProtocolDeleteFileW::Req req;
        req.FileName = appbox::WideToUTF8(marker);

        const auto rsp = ProbeDeleteFileW.Call(req, GetCWD(), config).get<ProtocolDeleteFileW::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The entry the marker hides is still gone from the view. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\" + kVictimName);

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The overlay still records the delete. */
    {
        const auto overlay = FolderInOverlay(GetCWDString());
        EXPECT_TRUE(std::filesystem::exists(overlay + L"\\" + kMarkerName));
    }

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
