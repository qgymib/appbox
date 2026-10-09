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
constexpr wchar_t kFolderName[] = L"AppBoxTest_StreamMerge";

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
 * 1. The lower layer holds `f.txt` with the alternate data stream
 *    `f.txt:stream`, while the host filesystem holds neither of them.
 * 2. The folder is isolated with `Merge`, so a modification of an entry the
 *    host filesystem does not hold stays inside the sandbox.
 * 3. The sandboxed process reads the stream of the packed file, writes it and
 *    reads the file back.
 *
 * Expected:
 * 1. The read of the stream reports the content of the lower layer, so a stream
 *    of a read-only layer is served by the view as well.
 * 2. The write lands in the overlay, and the packed file travels into it
 *    together with the stream, so the view keeps reporting the content of the
 *    lower layer for the file itself.
 * 3. The host filesystem gains no entry and the lower layer keeps its content.
 */
TEST_F(E2E_Fs, Stream_MergeKeepsThePackedStreamInTheSandbox)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"f.txt", "packed"),
                    FsFile(L"f.txt:stream", "packed-stream")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
                       appbox::FilesystemIsolation::Merge }
    }));

    /* The stream of the packed file is readable through the view. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\f.txt:stream");

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "packed-stream");
    }

    /* The write of the stream stays inside the sandbox, because the host
     * filesystem holds no such entry. */
    {
        ProtocolWriteFile::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\f.txt:stream");
        req.Data = "written";

        const auto rsp = ProbeWriteFile.Call(req, GetCWD(), config).get<ProtocolWriteFile::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "written");
    }

    /* The packed file travelled with its stream, so the view keeps reporting the
     * content of the lower layer for the file itself. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\f.txt");

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "packed");
    }

    /* The overlay holds the packed file and the stream which was written. */
    {
        const auto overlay = FolderInOverlay(GetCWDString());

        std::string data;
        ASSERT_EQ(ReadFileFull(overlay + L"\\f.txt", data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "packed");
        ASSERT_EQ(ReadFileFull(overlay + L"\\f.txt:stream", data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "written");
    }

    /* The host filesystem gained no entry, the lower layer keeps its content. */
    EXPECT_FALSE(host.FileExists(L"f.txt"));
    ASSERT_TRUE(tree.Verify());
}
