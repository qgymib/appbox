#include "probe/ReadFileFull.hpp"
#include "probe/WriteFile.hpp"
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
constexpr wchar_t kFolderName[] = L"AppBoxTest_StreamFull";

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
 *    `f.txt:stream`, and `g.txt` without a stream.
 * 2. Both files are isolated with `Full`, which keeps the host files readable
 *    and sends every write into the sandbox.
 * 3. The sandboxed process reads the stream of `f.txt`, writes it, creates a
 *    new stream on `g.txt` and reads both files back.
 *
 * Expected:
 * 1. The read of the stream reports the content of the host filesystem, which
 *    is what `Full` of a file keeps readable.
 * 2. The write of the stream and the create of the new stream land in the
 *    overlay, and the read back reports what was written.
 * 3. Both files travel into the overlay together with their streams, so the
 *    view keeps reporting the content of the host filesystem for the files
 *    themselves and the host filesystem is not modified.
 */
TEST_F(E2E_Fs, Stream_FullFileWritesIntoTheSandbox)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"f.txt", "host"));
    ASSERT_TRUE(host.WriteFile(L"f.txt:stream", "host-stream"));
    ASSERT_TRUE(host.WriteFile(L"g.txt", "g-host"));

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
                                            appbox::FilesystemEntryKind::File, appbox::FilesystemIsolation::Full },
                                           { L"#USERPROFILE#\\" + std::wstring(kFolderName) + L"\\g.txt",
                                            appbox::FilesystemEntryKind::File, appbox::FilesystemIsolation::Full }
    }));

    /* The stream of the file of the host filesystem is readable through the view. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\f.txt:stream");

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "host-stream");
    }

    /* The write of the stream lands in the sandbox. */
    {
        ProtocolWriteFile::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\f.txt:stream");
        req.Data = "written";

        const auto rsp = ProbeWriteFile.Call(req, GetCWD(), config).get<ProtocolWriteFile::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "written");
    }

    /* A stream the view does not hold yet is created in the sandbox as well. */
    {
        ProtocolWriteFile::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\g.txt:created");
        req.Data = "created";
        req.dwCreationDisposition = CREATE_NEW;

        const auto rsp = ProbeWriteFile.Call(req, GetCWD(), config).get<ProtocolWriteFile::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "created");
    }

    /* The files travelled with their streams, so the view still reports the
     * content of the host filesystem for them. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\f.txt");

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "host");
    }
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\g.txt");

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "g-host");
    }

    /* The overlay holds both files with the streams which were written. */
    {
        const auto overlay = FolderInOverlay(GetCWDString());

        std::string data;
        ASSERT_EQ(ReadFileFull(overlay + L"\\f.txt", data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "host");
        ASSERT_EQ(ReadFileFull(overlay + L"\\f.txt:stream", data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "written");
        ASSERT_EQ(ReadFileFull(overlay + L"\\g.txt", data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "g-host");
        ASSERT_EQ(ReadFileFull(overlay + L"\\g.txt:created", data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "created");
    }

    /* The host filesystem keeps its files and its stream, and it never gains the
     * stream the sandbox created. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull((host.Get() / L"f.txt").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "host");
        ASSERT_EQ(ReadFileFull((host.Get() / L"f.txt:stream").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "host-stream");
        ASSERT_EQ(ReadFileFull((host.Get() / L"g.txt").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "g-host");
        EXPECT_EQ(ReadFileFull((host.Get() / L"g.txt:created").wstring(), data),
                  static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
