#include "probe/CreateDirectoryW.hpp"
#include "probe/CreateFileW.hpp"
#include "probe/QueryAttributes.hpp"
#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/KnownFolder.hpp"
#include "WString.hpp"
#include <filesystem>

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

/** Name of the folder of this case below `#APPDATA#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_WhiteoutDir";

/**
 * Condition:
 * 1. The folder exists in a lower layer and its isolation mode is `Whiteout`.
 * 2. The sandboxed process queries the folder, reads a file below it and
 *    creates the folder and a file inside it.
 *
 * Expected:
 * 1. The folder and its whole subtree are invisible: the query, the read and
 *    the create report `File Not Found`.
 * 2. Creating the folder succeeds, and the folder is visible afterwards while
 *    the packed content stays hidden.
 */
TEST_F(Fs, Whiteout_FolderHidesItsSubtree)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {
            FsDir(L"filesystem\\#APPDATA#", {
                FsDir(kFolderName, {
                    FsFile(L"packed.txt", "packed")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(config, {
        { L"#APPDATA#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
          appbox::FilesystemIsolation::Whiteout }
    }));

    const auto folder = GetKnownFolderPath(L"#APPDATA#", false) + L"\\" + kFolderName;

    /* The folder and the file below it do not exist in the view. */
    {
        ProtocolQueryAttributes::Req req;
        req.FileName = appbox::WideToUTF8(folder);

        auto rsp = ProbeQueryAttributes.Call(req, GetCWD(), config).get<ProtocolQueryAttributes::Rsp>();
        EXPECT_EQ(rsp.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\packed.txt");

        auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* Creating the folder succeeds. */
    {
        ProtocolCreateDirectoryW::Req req;
        req.PathName = appbox::WideToUTF8(folder);

        auto rsp = ProbeCreateDirectoryW.Call(req, GetCWD(), config).get<ProtocolCreateDirectoryW::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    /* The folder the sandbox created is visible, the packed content is not. */
    {
        ProtocolQueryAttributes::Req req;
        req.FileName = appbox::WideToUTF8(folder);

        auto rsp = ProbeQueryAttributes.Call(req, GetCWD(), config).get<ProtocolQueryAttributes::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY));
    }
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\packed.txt");

        auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* A file inside the folder the sandbox created lands in the overlay. */
    {
        ProtocolCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\new.txt");
        req.dwDesiredAccess = GENERIC_WRITE;
        req.dwCreationDisposition = CREATE_NEW;

        auto rsp = ProbeCreateFileW.Call(req, GetCWD(), config).get<ProtocolCreateFileW::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    {
        const auto overlay =
            GetCWDString() + L"\\Upper\\filesystem\\" + GetKnownFolderPath(L"#APPDATA#", true) + L"\\" + kFolderName;
        ASSERT_TRUE(std::filesystem::exists(overlay));
        ASSERT_TRUE(std::filesystem::exists(overlay + L"\\new.txt"));
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
