#include "probe/ReparsePoint.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <cstddef>
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_ReparseDelete";

/**
 * Condition:
 * 1. The folder of the case holds the junction `link`, which names the folder
 *    `target` of the host, and its isolation mode is `Write Copy`.
 * 2. The sandboxed process queries the junction, reads the file of its target
 *    through it, removes the junction and queries the name again.
 *
 * Expected:
 * 1. The junction reports the attribute of a reparse point and the read
 *    through it reports the content of the target.
 * 2. The removal acts on the junction and not on the object it names: the name
 *    reports `File Not Found` afterwards, the target stays readable and the
 *    junction of the host filesystem survives, because a `Write Copy` delete is
 *    recorded in the overlay.
 */
TEST_F(E2E_Fs, ReparsePoint_DeleteOfALinkKeepsTheTarget)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"target\\data.txt", "target"));
    ASSERT_TRUE(host.CreateJunction(L"link", (host.Get() / L"target").wstring()));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem", {
                FsDir(L"#USERPROFILE#", {})
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

    ProtocolReparsePoint::Req::Item link;
    link.action = "attributes";
    link.path = appbox::WideToUTF8(folder + L"\\link");
    req.items.push_back(link);

    ProtocolReparsePoint::Req::Item through_link;
    through_link.action = "read_text";
    through_link.path = appbox::WideToUTF8(folder + L"\\link\\data.txt");
    req.items.push_back(through_link);

    ProtocolReparsePoint::Req::Item remove;
    remove.action = "remove_dir";
    remove.path = appbox::WideToUTF8(folder + L"\\link");
    req.items.push_back(remove);

    ProtocolReparsePoint::Req::Item link_after;
    link_after.action = "attributes";
    link_after.path = appbox::WideToUTF8(folder + L"\\link");
    req.items.push_back(link_after);

    ProtocolReparsePoint::Req::Item target_after;
    target_after.action = "read_text";
    target_after.path = appbox::WideToUTF8(folder + L"\\target\\data.txt");
    req.items.push_back(target_after);

    const auto rsp = ProbeReparsePoint.Call(req, GetCWD(), config).get<ProtocolReparsePoint::Rsp>();
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(5));

    /* The entry the caller spelled is the junction itself. */
    EXPECT_NE(rsp.items[0].attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_NE(rsp.items[0].attributes & FILE_ATTRIBUTE_REPARSE_POINT, static_cast<DWORD>(0));

    /* The call which opens the entry follows it. */
    EXPECT_EQ(rsp.items[1].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[1].text, "target");

    /* The removal reaches the junction and not the object it names. */
    EXPECT_EQ(rsp.items[2].code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.items[3].attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));

    EXPECT_EQ(rsp.items[4].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[4].text, "target");

    /*
     * The delete is recorded in the overlay and not performed on the host: the
     * junction of the host filesystem survives, the overlay holds the marker
     * which hides it, and the object the junction names is untouched.
     */
    const auto  host_link = host.Get() / L"link";
    const DWORD host_attributes = GetFileAttributesW(host_link.c_str());
    EXPECT_NE(host_attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_NE(host_attributes & FILE_ATTRIBUTE_REPARSE_POINT, static_cast<DWORD>(0));

    const auto overlay = std::filesystem::path(GetCWDString()) / L"data" / L"filesystem" /
                         GetKnownFolderPath(L"#USERPROFILE#", true) / kFolderName;
    EXPECT_FALSE(std::filesystem::exists(overlay / L"link"));
    EXPECT_TRUE(std::filesystem::exists(overlay / L"link.$APPBOX_DELETE$"));

    EXPECT_TRUE(host.FileExists(L"target\\data.txt"));

    ASSERT_TRUE(tree.Verify());
}
