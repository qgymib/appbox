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
constexpr wchar_t kFolderName[] = L"AppBoxTest_ReparseSandbox";

/**
 * Condition:
 * 1. The resources carry the folder `packed` with a file, which only a lower
 *    layer holds, and the folder of the case is isolated with `Write Copy`.
 * 2. The sandboxed process creates a junction which names the path of the view
 *    of the packed folder, reads the file through the junction, reads the data
 *    of the junction back, removes the data of the junction and asks the data
 *    of a file which carries no reparse point.
 *
 * Expected:
 * 1. The junction lands in the overlay, which is the layer a `Write Copy`
 *    modification uses.
 * 2. The read through the junction reports the content of the packed file: the
 *    view resolves the target of the junction in the view, so the layer
 *    mapping applies to the target as well.
 * 3. The data of the junction names the path of the view and not the path of
 *    the layer, which is what keeps the layout of the sandbox out of the view.
 * 4. The removal of the data succeeds and the entry is no longer a reparse
 *    point, while a control code which carries no reparse point reaches the
 *    file system unchanged.
 * 5. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, ReparsePoint_LinkInTheSandboxReachesALowerLayer)
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

    ProtocolReparsePoint::Req::Item create;
    create.action = "junction";
    create.path = appbox::WideToUTF8(folder + L"\\link");
    create.target = appbox::WideToUTF8(folder + L"\\packed");
    req.items.push_back(create);

    ProtocolReparsePoint::Req::Item read_through;
    read_through.action = "read_text";
    read_through.path = appbox::WideToUTF8(folder + L"\\link\\data.txt");
    req.items.push_back(read_through);

    ProtocolReparsePoint::Req::Item read_data;
    read_data.action = "read";
    read_data.path = appbox::WideToUTF8(folder + L"\\link");
    req.items.push_back(read_data);

    ProtocolReparsePoint::Req::Item remove_data;
    remove_data.action = "remove";
    remove_data.path = appbox::WideToUTF8(folder + L"\\link");
    req.items.push_back(remove_data);

    ProtocolReparsePoint::Req::Item attributes;
    attributes.action = "attributes";
    attributes.path = appbox::WideToUTF8(folder + L"\\link");
    req.items.push_back(attributes);

    /*
     * A control code which carries no reparse point reaches the object of the
     * layer unchanged: the file carries no data, so the file system reports
     * the failure of its own.
     */
    ProtocolReparsePoint::Req::Item not_a_link;
    not_a_link.action = "read";
    not_a_link.path = appbox::WideToUTF8(folder + L"\\packed\\data.txt");
    req.items.push_back(not_a_link);

    const auto rsp = ProbeReparsePoint.Call(req, GetCWD(), config).get<ProtocolReparsePoint::Rsp>();
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(6));

    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_SUCCESS));

    EXPECT_EQ(rsp.items[1].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[1].text, "packed");

    EXPECT_EQ(rsp.items[2].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[2].tag, static_cast<ULONG>(IO_REPARSE_TAG_MOUNT_POINT));
    EXPECT_EQ(rsp.items[2].substitute.compare(0, 4, "\\??\\"), 0);
    EXPECT_NE(rsp.items[2].substitute.find("\\packed"), std::string::npos);
    EXPECT_EQ(rsp.items[2].substitute.find("\\data\\filesystem\\"), std::string::npos);

    EXPECT_EQ(rsp.items[3].status, static_cast<long>(STATUS_SUCCESS));

    EXPECT_NE(rsp.items[4].attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_EQ(rsp.items[4].attributes & FILE_ATTRIBUTE_REPARSE_POINT, static_cast<DWORD>(0));

    EXPECT_EQ(rsp.items[5].status, static_cast<long>(STATUS_NOT_A_REPARSE_POINT));

    /* The junction is an entry of the overlay. */
    const std::filesystem::path overlay = std::filesystem::path(GetCWDString()) / L"data" / L"filesystem" /
                                          GetKnownFolderPath(L"#USERPROFILE#", true) / kFolderName / L"link";
    EXPECT_TRUE(std::filesystem::exists(overlay));

    ASSERT_TRUE(tree.Verify());
}
