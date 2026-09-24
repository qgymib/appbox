#include "probe/CreateFileW.hpp"
#include "probe/ListDir.hpp"
#include "probe/QueryAttributes.hpp"
#include "probe/ReadFileFull.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/KnownFolder.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/RealFsFolder.hpp"
#include "WString.hpp"
#include <algorithm>
#include <filesystem>

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

/** Name of the folder of this case below `#APPDATA#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_Create";

/**
 * Condition:
 * 1. The file exists in a lower layer and in the host, and its isolation mode
 *    is `Whiteout`.
 * 2. The sandboxed process creates the file and reads it back.
 *
 * Expected:
 * 1. The create succeeds and lands in the overlay of the sandbox.
 * 2. The entry is visible afterwards, and the host file and the packed content
 *    stay hidden and unchanged.
 */
TEST_F(Fs, Whiteout_CreateInSandbox)
{
    RealFsFolder host(L"#APPDATA#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"data.txt", "host"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {
            FsDir(L"filesystem\\#APPDATA#", {
                FsDir(kFolderName, {
                    FsFile(L"data.txt", "packed")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(config, {
        { L"#APPDATA#\\" + std::wstring(kFolderName) + L"\\data.txt", appbox::FilesystemEntryKind::File,
          appbox::FilesystemIsolation::Whiteout }
    }));

    const auto file = GetKnownFolderPath(L"#APPDATA#", false) + L"\\" + kFolderName + L"\\data.txt";

    /* The entry does not exist in the view. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(file);

        auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* Creating it succeeds and lands in the sandbox. */
    {
        ProtocolCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(file);
        req.dwDesiredAccess = GENERIC_WRITE;
        req.dwCreationDisposition = CREATE_NEW;

        auto rsp = ProbeCreateFileW.Call(req, GetCWD(), config).get<ProtocolCreateFileW::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    /* The entry the sandbox holds itself is visible from now on. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(file);

        auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_TRUE(rsp.data.empty());
    }
    {
        ProtocolQueryAttributes::Req req;
        req.FileName = appbox::WideToUTF8(file);

        auto rsp = ProbeQueryAttributes.Call(req, GetCWD(), config).get<ProtocolQueryAttributes::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(0));
    }
    {
        ProtocolListDir::Req req;
        req.path = appbox::WideToUTF8(GetKnownFolderPath(L"#APPDATA#", false) + L"\\" + kFolderName);
        req.method = ProtocolListDir::Req::Method::WinAPI;

        auto rsp = ProbeListDir.Call(req, GetCWD(), config).get<ProtocolListDir::Rsp>();
        EXPECT_TRUE(std::any_of(rsp.entries.begin(), rsp.entries.end(),
                                [](const ProtocolListDir::Rsp::Entry& entry) { return entry.name == "data.txt"; }));
    }

    /* The file of the overlay was created. */
    {
        const auto overlay = GetCWDString() + L"\\Upper\\filesystem\\" + GetKnownFolderPath(L"#APPDATA#", true) +
                             L"\\" + kFolderName + L"\\data.txt";
        ASSERT_TRUE(std::filesystem::exists(overlay));
    }

    /* The host file keeps its content. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull((host.Get() / L"data.txt").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "host");
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
