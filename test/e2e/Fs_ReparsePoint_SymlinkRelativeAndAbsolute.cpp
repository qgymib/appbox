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
 *
 * A symbolic link carries a target which is relative to the link, which a
 * junction cannot express, so the case creates one instead of using the
 * junction a helper of the host provides. The kernel asks the caller for
 * `SeCreateSymbolicLinkPrivilege` to create one, which an unelevated process
 * holds only while the machine runs in developer mode: a machine which grants
 * neither refuses the creation with `ERROR_PRIVILEGE_NOT_HELD`, and the case
 * skips itself instead of reporting the refusal of the machine as an answer of
 * the view.
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
    relative.action = ProtocolReparsePoint::Req::Item::Action::Symlink;
    relative.path = appbox::WideToUTF8(folder + L"\\relative_link");
    relative.target = "packed";
    relative.relative = true;
    req.items.push_back(relative);

    ProtocolReparsePoint::Req::Item read_relative;
    read_relative.action = ProtocolReparsePoint::Req::Item::Action::ReadText;
    read_relative.path = appbox::WideToUTF8(folder + L"\\relative_link\\data.txt");
    req.items.push_back(read_relative);

    ProtocolReparsePoint::Req::Item read_relative_data;
    read_relative_data.action = ProtocolReparsePoint::Req::Item::Action::Read;
    read_relative_data.path = appbox::WideToUTF8(folder + L"\\relative_link");
    req.items.push_back(read_relative_data);

    ProtocolReparsePoint::Req::Item absolute;
    absolute.action = ProtocolReparsePoint::Req::Item::Action::Symlink;
    absolute.path = appbox::WideToUTF8(folder + L"\\absolute_link");
    absolute.target = appbox::WideToUTF8(folder + L"\\packed");
    req.items.push_back(absolute);

    ProtocolReparsePoint::Req::Item read_absolute;
    read_absolute.action = ProtocolReparsePoint::Req::Item::Action::ReadText;
    read_absolute.path = appbox::WideToUTF8(folder + L"\\absolute_link\\data.txt");
    req.items.push_back(read_absolute);

    ProtocolReparsePoint::Req::Item read_absolute_data;
    read_absolute_data.action = ProtocolReparsePoint::Req::Item::Action::Read;
    read_absolute_data.path = appbox::WideToUTF8(folder + L"\\absolute_link");
    req.items.push_back(read_absolute_data);

    const auto rsp = ProbeReparsePoint.Call(req, GetCWD(), config).get<ProtocolReparsePoint::Rsp>();
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(6));

    /*
     * The first item creates the symbolic link, which the kernel serves only for
     * a caller whose token holds `SeCreateSymbolicLinkPrivilege` (or while the
     * machine runs in developer mode). A process which holds neither reaches
     * `ERROR_PRIVILEGE_NOT_HELD` here, and the machine refuses every symbolic
     * link the same way: the case skips itself instead of reporting the refusal
     * of the machine as an answer of the view. A junction would not depend on
     * the privilege, but it cannot carry a target which is relative to the link.
     */
    if (rsp.items[0].code == ERROR_PRIVILEGE_NOT_HELD)
    {
        GTEST_SKIP() << "the case needs a symbolic link, which this process may not create: run the suite from an "
                        "elevated shell or turn on developer mode";
    }

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
