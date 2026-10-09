#include "probe/ListDir.hpp"
#include "probe/QueryAttributes.hpp"
#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <CLI/Encoding.hpp>
#include <string>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_MarkerBelow";

/** Name of the folder of the lower layer which carries a reserved name. */
constexpr wchar_t kReservedName[] = L"hidden.$APPBOX_DELETE$";

/**
 * @brief Get the path of the folder of the case inside the view.
 * @return Path of the folder.
 */
std::wstring FolderPath()
{
    return GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
}

} // namespace

/**
 * Condition:
 * 1. The lower layer holds a folder whose own name is reserved
 *    (`hidden.$APPBOX_DELETE$`) with a file below it.
 * 2. The folder of the case isolates with `Write Copy`.
 * 3. The sandboxed process looks the reserved name up, reads the file below it
 *    and lists the folder which holds it.
 *
 * Expected:
 * 1. The reserved name is no entry of the view: the lookup reports
 *    `File Not Found` and a path below it reports `Path Not Found`.
 * 2. The listing of the folder above it drops the name as well.
 */
TEST_F(E2E_Fs, Marker_PathBelowAReservedName)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsDir(kReservedName, {
                        FsFile(L"inside.txt", "packed")
                    })
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

    /* The name of the marker is no entry of the view. */
    {
        ProtocolQueryAttributes::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\" + kReservedName);

        const auto rsp = ProbeQueryAttributes.Call(req, GetCWD(), config).get<ProtocolQueryAttributes::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
        EXPECT_EQ(rsp.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    }

    /* Nothing hangs below a name the view reserves. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\" + kReservedName + L"\\inside.txt");

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_PATH_NOT_FOUND));
    }

    /* The listing of the folder above the reserved name drops it as well. */
    {
        ProtocolListDir::Req req;
        req.path = CLI::narrow(FolderPath());
        req.method = ProtocolListDir::Req::Method::Std;

        ProtocolListDir::Rsp rsp;
        ProbeListDir.Call(req, GetCWD(), config).get_to(rsp);
        EXPECT_TRUE(rsp.entries.empty());
    }

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
