#include "probe/ReparsePoint.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <cstddef>
#include <string>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_ReparseSymlink";

/**
 * Condition:
 * 1. The resources carry the folder `packed` with a file, which only a lower
 *    layer holds, and the folder of the case is isolated with `Write Copy`.
 * 2. The sandboxed process creates a symbolic link whose target is relative to
 *    the directory of the link, and a second one whose target is the path of
 *    the view of the packed folder, and reads the file through both of them.
 *
 * Expected:
 * 1. Both links report the content of the packed file, so the view resolves
 *    the two shapes of the target.
 * 2. The data of the relative link carries the relative name the caller wrote,
 *    because the view maps a layer by replacing the prefix of the path: a
 *    target which is relative to the link names the same entry in the view.
 * 3. The data of the absolute link carries the path of the view.
 * 4. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, ReparsePoint_SymlinkRelativeAndAbsolute)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsDir(L"packed", {
                        FsFile(L"data.txt", "packed")
                    })
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const std::wstring folder = GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
                       appbox::FilesystemIsolation::WriteCopy }
    }));

    ProtocolReparsePoint::Req req;

    ProtocolReparsePoint::Req::Item relative;
    relative.action = "symlink";
    relative.path = appbox::WideToUTF8(folder + L"\\relative_link");
    relative.target = "packed";
    relative.relative = true;
    req.items.push_back(relative);

    ProtocolReparsePoint::Req::Item read_relative;
    read_relative.action = "read_text";
    read_relative.path = appbox::WideToUTF8(folder + L"\\relative_link\\data.txt");
    req.items.push_back(read_relative);

    ProtocolReparsePoint::Req::Item read_relative_data;
    read_relative_data.action = "read";
    read_relative_data.path = appbox::WideToUTF8(folder + L"\\relative_link");
    req.items.push_back(read_relative_data);

    ProtocolReparsePoint::Req::Item absolute;
    absolute.action = "symlink";
    absolute.path = appbox::WideToUTF8(folder + L"\\absolute_link");
    absolute.target = appbox::WideToUTF8(folder + L"\\packed");
    req.items.push_back(absolute);

    ProtocolReparsePoint::Req::Item read_absolute;
    read_absolute.action = "read_text";
    read_absolute.path = appbox::WideToUTF8(folder + L"\\absolute_link\\data.txt");
    req.items.push_back(read_absolute);

    ProtocolReparsePoint::Req::Item read_absolute_data;
    read_absolute_data.action = "read";
    read_absolute_data.path = appbox::WideToUTF8(folder + L"\\absolute_link");
    req.items.push_back(read_absolute_data);

    const auto rsp = ProbeReparsePoint.Call(req, GetCWD(), config).get<ProtocolReparsePoint::Rsp>();
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(6));

    EXPECT_EQ(rsp.items[0].code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.items[1].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[1].text, "packed");

    EXPECT_EQ(rsp.items[2].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[2].tag, static_cast<ULONG>(IO_REPARSE_TAG_SYMLINK));
    EXPECT_EQ(rsp.items[2].substitute, "packed");

    EXPECT_EQ(rsp.items[3].code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.items[4].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[4].text, "packed");

    EXPECT_EQ(rsp.items[5].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[5].tag, static_cast<ULONG>(IO_REPARSE_TAG_SYMLINK));
    EXPECT_EQ(rsp.items[5].substitute.compare(0, 4, "\\??\\"), 0);
    EXPECT_NE(rsp.items[5].substitute.find("\\packed"), std::string::npos);
    EXPECT_EQ(rsp.items[5].substitute.find("\\data\\filesystem\\"), std::string::npos);

    ASSERT_TRUE(tree.Verify());
}
