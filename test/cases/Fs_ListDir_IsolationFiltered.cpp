#include "probe/ListDir.hpp"
#include "probe/ListDirNt.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/KnownFolder.hpp"
#include "utils/RealFsFolder.hpp"
#include "WString.hpp"
#include <algorithm>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

/** Name of the folder of this case below `#APPDATA#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_List";

/**
 * @brief Assert that an enumeration reported the visible entry only.
 * @param[in] names Names the enumeration reported.
 */
void ExpectVisibleEntriesOnly(const std::vector<std::string>& names)
{
    EXPECT_EQ(names.size(), 1u);
    EXPECT_NE(std::find(names.begin(), names.end(), "visible.txt"), names.end());
    EXPECT_EQ(std::find(names.begin(), names.end(), "hidden.txt"), names.end());
    EXPECT_EQ(std::find(names.begin(), names.end(), "host.txt"), names.end());
    EXPECT_EQ(std::find(names.begin(), names.end(), "hostdir"), names.end());
}

/**
 * Condition:
 * 1. The folder is isolated with `Full` and the file `hidden.txt` below it with
 *    `Whiteout`; the folder and a file below it exist in the host.
 * 2. The sandboxed process enumerates the folder with the user mode wrappers
 *    and with both NT entry points.
 *
 * Expected:
 * 1. Every enumeration reports the visible entry of the lower layer only: the
 *    hidden file, the host file and the host folder are not listed, and the
 *    two NT entry points agree.
 * 2. The host folder and the lower layers are unchanged.
 */
TEST_F(Fs, ListDir_IsolationFiltersTheEntries)
{
    RealFsFolder host(L"#APPDATA#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"host.txt", "host"));
    ASSERT_TRUE(host.WriteFile(L"hostdir\\host.txt", "host"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {
            FsDir(L"filesystem\\#APPDATA#", {
                FsDir(kFolderName, {
                    FsFile(L"visible.txt", "packed"),
                    FsFile(L"hidden.txt", "packed")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(config, {
        { L"#APPDATA#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
          appbox::FilesystemIsolation::Full },
        { L"#APPDATA#\\" + std::wstring(kFolderName) + L"\\hidden.txt", appbox::FilesystemEntryKind::File,
          appbox::FilesystemIsolation::Whiteout }
    }));

    const auto folder = GetKnownFolderPath(L"#APPDATA#", false) + L"\\" + kFolderName;

    /* The user mode wrappers report the visible entry only. */
    for (const auto method : { ProtocolListDir::Req::Method::Std, ProtocolListDir::Req::Method::WinAPI,
                               ProtocolListDir::Req::Method::CRT })
    {
        ProtocolListDir::Req req;
        req.path = appbox::WideToUTF8(folder);
        req.method = method;

        auto rsp = ProbeListDir.Call(req, GetCWD(), config).get<ProtocolListDir::Rsp>();

        std::vector<std::string> names;
        for (const auto& entry : rsp.entries)
        {
            names.push_back(entry.name);
        }
        ExpectVisibleEntriesOnly(names);
    }

    /* Both NT entry points agree with the wrappers. */
    for (const bool extended : { false, true })
    {
        ProtocolListDirNt::Req req;
        req.path = appbox::WideToUTF8(folder);
        req.extended = extended;

        auto rsp = ProbeListDirNt.Call(req, GetCWD(), config).get<ProtocolListDirNt::Rsp>();
        ASSERT_EQ(rsp.status, 0L);
        ExpectVisibleEntriesOnly(rsp.names);
    }

    /* The host folder was not touched. */
    EXPECT_TRUE(host.FileExists(L"host.txt"));
    EXPECT_TRUE(host.FileExists(L"hostdir\\host.txt"));

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
