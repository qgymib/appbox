#include "probe/DeleteFileW.hpp"
#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <string>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_StreamDelete";

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
 * 1. The host filesystem holds `f.txt` with the alternate data stream
 *    `f.txt:stream`.
 * 2. The file is isolated with `Full`, so a delete of the stream is recorded in
 *    the sandbox and the host filesystem stays untouched.
 * 3. The sandboxed process deletes the stream and reads the file and the stream
 *    afterwards.
 *
 * Expected:
 * 1. The delete succeeds and records the delete in the overlay: the marker of a
 *    stream is a stream of the file itself, so the file travels into the
 *    overlay with the content the view reports for it.
 * 2. The stream reports `File Not Found` afterwards, while the file keeps the
 *    content of the host filesystem.
 * 3. The host file and its stream are unchanged.
 */
TEST_F(E2E_Fs, Stream_DeleteOfAStreamWritesTheMarker)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"f.txt", "host"));
    ASSERT_TRUE(host.WriteFile(L"f.txt:stream", "host-stream"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {})
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(
        WriteFsIsolationFile(GetCWD(), {
                                           { L"#USERPROFILE#\\" + std::wstring(kFolderName) + L"\\f.txt",
                                            appbox::FilesystemEntryKind::File, appbox::FilesystemIsolation::Full }
    }));

    /* The delete of the stream is recorded in the sandbox. */
    {
        ProtocolDeleteFileW::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\f.txt:stream");

        const auto rsp = ProbeDeleteFileW.Call(req, GetCWD(), config).get<ProtocolDeleteFileW::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    /* The stream is gone from the view. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\f.txt:stream");

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The file keeps the content of the host filesystem. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\f.txt");

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "host");
    }

    /* The overlay holds the file with its content and the marker of the stream,
     * which is a stream of that file. */
    {
        const auto overlay = FolderInOverlay(GetCWDString());

        std::string data;
        ASSERT_EQ(ReadFileFull(overlay + L"\\f.txt", data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "host");
        EXPECT_NE(GetFileAttributesW((overlay + L"\\f.txt:stream.$APPBOX_DELETE$").c_str()),
                  static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    }

    /* The host filesystem keeps the file and its stream. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull((host.Get() / L"f.txt").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "host");
        ASSERT_EQ(ReadFileFull((host.Get() / L"f.txt:stream").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "host-stream");
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
