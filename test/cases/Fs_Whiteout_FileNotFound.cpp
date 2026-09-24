#include "probe/DeleteFileW.hpp"
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

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

/** Name of the folder of this case below `#APPDATA#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_Whiteout";

/**
 * Condition:
 * 1. The file exists in a lower layer and in the host, and its isolation mode
 *    is `Whiteout`.
 * 2. The sandboxed process reads the file, queries its attributes, deletes it
 *    and lists the folder.
 *
 * Expected:
 * 1. Every call reports `File Not Found`, and the file is not listed.
 * 2. The entry which is not hidden keeps its read through, and neither the
 *    host file nor the packed content is modified.
 */
TEST_F(Fs, Whiteout_FileIsNotFound)
{
    RealFsFolder host(L"#APPDATA#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"hidden.txt", "host"));
    ASSERT_TRUE(host.WriteFile(L"visible.txt", "host-visible"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {
            FsDir(L"filesystem\\#APPDATA#", {
                FsDir(kFolderName, {
                    FsFile(L"hidden.txt", "packed"),
                    FsFile(L"visible.txt", "packed-visible")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(config, {
        { L"#APPDATA#\\" + std::wstring(kFolderName) + L"\\hidden.txt", appbox::FilesystemEntryKind::File,
          appbox::FilesystemIsolation::Whiteout }
    }));

    const auto folder = GetKnownFolderPath(L"#APPDATA#", false) + L"\\" + kFolderName;

    /* The hidden file cannot be read, the other file keeps its read through. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\hidden.txt");

        auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\visible.txt");

        auto rsp = ProbeReadFileFull.Call(req, GetCWD(), config).get<ProtocolReadFileFull::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "packed-visible");
    }

    /* The attributes of the hidden file are not reported either. */
    {
        ProtocolQueryAttributes::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\hidden.txt");

        auto rsp = ProbeQueryAttributes.Call(req, GetCWD(), config).get<ProtocolQueryAttributes::Rsp>();
        EXPECT_EQ(rsp.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The hidden file cannot be deleted. */
    {
        ProtocolDeleteFileW::Req req;
        req.FileName = appbox::WideToUTF8(folder + L"\\hidden.txt");

        auto rsp = ProbeDeleteFileW.Call(req, GetCWD(), config).get<ProtocolDeleteFileW::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The listing does not report the hidden file. */
    {
        ProtocolListDir::Req req;
        req.path = appbox::WideToUTF8(folder);
        req.method = ProtocolListDir::Req::Method::WinAPI;

        auto rsp = ProbeListDir.Call(req, GetCWD(), config).get<ProtocolListDir::Rsp>();

        const auto named = [&rsp](const std::string& name) {
            return std::any_of(rsp.entries.begin(), rsp.entries.end(),
                               [&name](const ProtocolListDir::Rsp::Entry& entry) { return entry.name == name; });
        };

        EXPECT_TRUE(named("visible.txt"));
        EXPECT_FALSE(named("hidden.txt"));
    }

    /* The host file and the packed file were not modified. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull((host.Get() / L"hidden.txt").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "host");
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
