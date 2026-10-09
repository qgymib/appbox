#include "probe/DeleteFileW.hpp"
#include "probe/QueryAttributes.hpp"
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
constexpr wchar_t kFolderName[] = L"AppBoxTest_StreamWhiteout";

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
 * 1. The host filesystem holds `f.txt` with the alternate data stream
 *    `f.txt:stream`.
 * 2. The file is isolated with `Whiteout`, so the entry is invisible until the
 *    sandbox holds it itself.
 * 3. The sandboxed process reads the stream, queries it, deletes it and creates
 *    another stream of the file.
 *
 * Expected:
 * 1. The read, the query and the delete of the stream report `File Not Found`,
 *    because the mode of the file covers the streams the file carries.
 * 2. The create of the other stream succeeds and lands in the overlay, so the
 *    entry is visible afterwards.
 * 3. The host file and its stream are unchanged.
 */
TEST_F(E2E_Fs, Stream_WhiteoutFileHidesItsStream)
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
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName) + L"\\f.txt", appbox::FilesystemEntryKind::File,
                       appbox::FilesystemIsolation::Whiteout }
    }));

    /* The stream of the hidden file is not part of the view. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\f.txt:stream");

        const auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }
    {
        ProtocolQueryAttributes::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\f.txt:stream");

        const auto rsp = ProbeQueryAttributes.Call(req, GetCWD(), config).get<ProtocolQueryAttributes::Rsp>();
        EXPECT_EQ(rsp.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }
    {
        ProtocolDeleteFileW::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\f.txt:stream");

        const auto rsp = ProbeDeleteFileW.Call(req, GetCWD(), config).get<ProtocolDeleteFileW::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The entry the sandbox creates itself is visible afterwards. */
    {
        ProtocolWriteFile::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\f.txt:created");
        req.Data = "created";
        req.dwCreationDisposition = CREATE_NEW;

        const auto rsp = ProbeWriteFile.Call(req, GetCWD(), config).get<ProtocolWriteFile::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "created");
    }

    /* The host file and its stream are unchanged. */
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
