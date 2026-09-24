#include "probe/CreateFileW.hpp"
#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/KnownFolder.hpp"
#include "utils/RealFsFolder.hpp"
#include "WString.hpp"
#include <filesystem>

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

/** Name of the folder of this case below `#APPDATA#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_Override";

/**
 * Condition:
 * 1. The folder is isolated with `Full` and the folder `data` below it with
 *    `Write Copy`.
 * 2. Both folders exist in the host, the packed content of `data` is in a
 *    lower layer.
 * 3. The sandboxed process reads the host files of both folders and creates a
 *    file inside `data`.
 *
 * Expected:
 * 1. The folder which is `Full` hides the host, the folder below it which is
 *    `Write Copy` shows the host again: the mode of a folder below overrides
 *    the mode of the folder above for its own subtree.
 * 2. The virtual content wins over the host content, and the new file lands in
 *    the overlay.
 */
TEST_F(Fs, Full_SubFolderWriteCopyShowsTheHost)
{
    RealFsFolder host(L"#APPDATA#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"host.txt", "host"));
    ASSERT_TRUE(host.WriteFile(L"data\\host.txt", "host-data"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {
            FsDir(L"filesystem\\#APPDATA#", {
                FsDir(kFolderName, {
                    FsDir(L"data", {
                        FsFile(L"packed.txt", "packed")
                    })
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(config, {
        { L"#APPDATA#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
          appbox::FilesystemIsolation::Full },
        { L"#APPDATA#\\" + std::wstring(kFolderName) + L"\\data", appbox::FilesystemEntryKind::Directory,
          appbox::FilesystemIsolation::WriteCopy }
    }));

    const auto folder = GetKnownFolderPath(L"#APPDATA#", false) + L"\\" + kFolderName;

    /* The folder above is `Full`: its host content is hidden. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\host.txt");

        auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The folder below is `Write Copy`: the host content is visible again. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\data\\host.txt");

        auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "host-data");
    }

    /* The virtual content of the folder below wins over the host content. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\data\\packed.txt");

        auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "packed");
    }

    /* A file which the sandbox creates lands in the overlay. */
    {
        ProtocolCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\data\\new.txt");
        req.dwDesiredAccess = GENERIC_WRITE;
        req.dwCreationDisposition = CREATE_NEW;

        auto rsp = ProbeCreateFileW.Call(req, GetCWD(), config).get<ProtocolCreateFileW::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    {
        const auto overlay = GetCWDString() + L"\\Upper\\filesystem\\" + GetKnownFolderPath(L"#APPDATA#", true) +
                             L"\\" + kFolderName + L"\\data\\new.txt";
        ASSERT_TRUE(std::filesystem::exists(overlay));
    }

    /* The host folders were not touched. */
    EXPECT_TRUE(host.FileExists(L"host.txt"));
    EXPECT_TRUE(host.FileExists(L"data\\host.txt"));
    EXPECT_FALSE(host.FileExists(L"data\\new.txt"));

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
