#include "probe/CreateFileW.hpp"
#include "probe/ListDir.hpp"
#include "probe/QueryAttributes.hpp"
#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/KnownFolder.hpp"
#include "utils/RealFsFolder.hpp"
#include "WString.hpp"
#include <algorithm>
#include <filesystem>

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

/** Name of the folder of this case below `#APPDATA#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_Full";

/**
 * Condition:
 * 1. The folder exists in a lower layer and in the host, and its isolation
 *    mode is `Full`.
 * 2. The sandboxed process reads the files of the folder, queries the
 *    attributes of a folder which only the host holds and lists the folder.
 *
 * Expected:
 * 1. The content of the lower layer stays visible, the content of the host
 *    folder does not: reading a host file and querying a host folder report
 *    `File Not Found`, and the host entries are not listed.
 * 2. A file which is created inside the folder lands in the overlay and the
 *    host folder is unchanged.
 */
TEST_F(Fs, Full_HidesTheHostFolder)
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
                    FsFile(L"packed.txt", "packed")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(config, {
        { L"#APPDATA#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
          appbox::FilesystemIsolation::Full }
    }));

    const auto folder = GetKnownFolderPath(L"#APPDATA#", false) + L"\\" + kFolderName;

    /* The content of the lower layer stays visible. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\packed.txt");

        auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "packed");
    }

    /* The content of the host folder is not. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\host.txt");

        auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* A folder which only the host holds is not visible either. */
    {
        ProtocolQueryAttributes::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\data");

        auto rsp = ProbeQueryAttributes.Call(req, GetCWD(), config).get<ProtocolQueryAttributes::Rsp>();
        EXPECT_EQ(rsp.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The listing shows the virtual content and none of the host entries. */
    {
        ProtocolListDir::Req req;
        req.path = appbox::WideToUTF8(folder);
        req.method = ProtocolListDir::Req::Method::WinAPI;

        auto rsp = ProbeListDir.Call(req, GetCWD(), config).get<ProtocolListDir::Rsp>();

        const auto named = [&rsp](const std::string& name) {
            return std::any_of(rsp.entries.begin(), rsp.entries.end(),
                               [&name](const ProtocolListDir::Rsp::Entry& entry) { return entry.name == name; });
        };

        EXPECT_TRUE(named("packed.txt"));
        EXPECT_FALSE(named("host.txt"));
        EXPECT_FALSE(named("data"));
    }

    /* A file which the sandbox creates lands in the overlay. */
    {
        ProtocolCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\new.txt");
        req.dwDesiredAccess = GENERIC_WRITE;
        req.dwCreationDisposition = CREATE_NEW;

        auto rsp = ProbeCreateFileW.Call(req, GetCWD(), config).get<ProtocolCreateFileW::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    {
        const auto overlay = GetCWDString() + L"\\Upper\\filesystem\\" + GetKnownFolderPath(L"#APPDATA#", true) +
                             L"\\" + kFolderName + L"\\new.txt";
        ASSERT_TRUE(std::filesystem::exists(overlay));
    }

    /* The host folder was not touched. */
    EXPECT_TRUE(host.FileExists(L"host.txt"));
    EXPECT_TRUE(host.FileExists(L"data\\host.txt"));
    EXPECT_FALSE(host.FileExists(L"new.txt"));

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
