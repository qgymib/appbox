#include "probe/CreateFileW.hpp"
#include "probe/ListDir.hpp"
#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <CLI/Encoding.hpp>
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_MarkerOpaque";

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
 * 1. The lower layer holds the folder of the case with `inside.txt`, and the
 *    folder isolates with `Write Copy`.
 * 2. The sandboxed process tries to create the opaque marker of that folder
 *    itself.
 *
 * Expected:
 * 1. The creation is refused with `Invalid Name`, because the names of the
 *    markers are reserved by the view.
 * 2. The content of the folder stays visible: the file of the lower layer is
 *    listed and readable, and the overlay holds no marker.
 */
TEST_F(E2E_Fs, Marker_ForgeOpaqueIsRefused)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"inside.txt", "packed")
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

    /* The marker which would make the folder opaque. */
    {
        ProtocolCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\.$APPBOX_OPAQUE$");
        req.dwDesiredAccess = GENERIC_WRITE;
        req.dwCreationDisposition = CREATE_NEW;

        const auto rsp = ProbeCreateFileW.Call(req, GetCWD(), config).get<ProtocolCreateFileW::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_INVALID_NAME));
    }

    /* The folder is not opaque: the content of the lower layer stays visible. */
    {
        ProtocolListDir::Req req;
        req.path = CLI::narrow(FolderPath());
        req.method = ProtocolListDir::Req::Method::Std;

        ProtocolListDir::Rsp rsp;
        ProbeListDir.Call(req, GetCWD(), config).get_to(rsp);
        ASSERT_EQ(rsp.entries.size(), 1u);
        EXPECT_EQ(rsp.entries[0].name, CLI::narrow(L"inside.txt"));
    }
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\inside.txt");

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "packed");
    }

    /* The overlay holds no marker. */
    {
        const auto overlay = FolderInOverlay(GetCWDString());
        EXPECT_FALSE(std::filesystem::exists(overlay + L"\\.$APPBOX_OPAQUE$"));
    }

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
