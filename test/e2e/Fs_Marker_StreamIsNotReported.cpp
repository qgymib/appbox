#include "probe/CreateFileW.hpp"
#include "probe/FindStreams.hpp"
#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_MarkerStream";

/** Name of the file the case enumerates. */
constexpr wchar_t kFileName[] = L"f.txt";

/** Name of the stream the view records the delete of a stream with. */
constexpr wchar_t kMarkerStream[] = L"gone.$APPBOX_DELETE$";

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
 * @brief Whether one of the names carries a text.
 * @param[in] names Names to inspect.
 * @param[in] text Text to look for.
 * @return true when one of the names carries the text.
 */
bool Carries(const std::vector<std::string>& names, const std::string& text)
{
    for (const auto& name : names)
    {
        if (name.find(text) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

} // namespace

/**
 * Condition:
 * 1. The overlay holds `f.txt` with the stream `note` and with the marker
 *    stream `gone.$APPBOX_DELETE$`, which is what the view records the delete
 *    of a stream with.
 * 2. The folder of the case isolates with `Write Copy`.
 * 3. The sandboxed process enumerates the streams of the file, reads the
 *    marker stream and creates a stream whose name is reserved.
 *
 * Expected:
 * 1. The enumeration reports the streams of the file and never the marker of
 *    the view.
 * 2. The marker stream reports `File Not Found` and a creation of a reserved
 *    stream name is refused with `Invalid Name`.
 * 3. The overlay keeps the marker stream.
 */
TEST_F(E2E_Fs, Marker_StreamIsNotReported)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {
            FsDir(L"filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true), {
                FsDir(kFolderName, {
                    FsFile(L"f.txt", "content"),
                    FsFile(L"f.txt:note", "note"),
                    FsFile(L"f.txt:gone.$APPBOX_DELETE$", "")
                })
            })
        }),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {})
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
                       appbox::FilesystemIsolation::WriteCopy }
    }));

    /* The enumeration reports the streams of the file without the marker. */
    {
        ProtocolFindStreams::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\" + kFileName);

        const auto rsp = ProbeFindStreams.Call(req, GetCWD(), config).get<ProtocolFindStreams::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.names.size(), 2u);
        EXPECT_TRUE(Carries(rsp.names, "note"));
        EXPECT_FALSE(Carries(rsp.names, "APPBOX_DELETE"));
    }

    /* The marker stream cannot be read. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\" + kFileName + L":" + kMarkerStream);

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* A stream whose name is reserved cannot be created. */
    {
        ProtocolCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\" + kFileName + L":stream.$APPBOX_DELETE$");
        req.dwDesiredAccess = GENERIC_WRITE;
        req.dwCreationDisposition = CREATE_NEW;

        const auto rsp = ProbeCreateFileW.Call(req, GetCWD(), config).get<ProtocolCreateFileW::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_INVALID_NAME));
    }

    /* The overlay keeps the marker stream of the view. */
    {
        const auto overlay = FolderInOverlay(GetCWDString());
        const auto marker = overlay + L"\\" + kFileName + L":" + kMarkerStream;
        EXPECT_NE(GetFileAttributesW(marker.c_str()), static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    }

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
