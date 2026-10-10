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
constexpr wchar_t kFolderName[] = L"AppBoxTest_ReparseUnresolved";

/**
 * Name of the target of the junction of this case.
 *
 * The name addresses a volume which the machine does not hold, so it cannot be
 * turned into a path of the view.
 */
constexpr wchar_t kUnresolvableTarget[] = L"\\??\\Volume{00000000-0000-0000-0000-000000000000}\\x";

/**
 * Condition:
 * 1. The folder of the case holds the junction `link`, whose target names a
 *    volume which cannot be expressed as a path of the view, and its isolation
 *    mode is `Write Copy`.
 * 2. The sandboxed process opens a file through the junction and queries its
 *    attributes.
 *
 * Expected:
 * 1. Both calls report the failure of the view instead of the answer of the
 *    layer: the view resolves the reparse points of a path itself, so it must
 *    not let the file system of the layer follow a link it cannot resolve.
 * 2. The junction itself stays an entry of the view, because a call which acts
 *    on the entry does not follow it.
 */
TEST_F(E2E_Fs, ReparsePoint_UnresolvableTargetFails)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.CreateJunction(L"link", kUnresolvableTarget));

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

    ProtocolReparsePoint::Req::Item open_through;
    open_through.action = "read_text";
    open_through.path = appbox::WideToUTF8(folder + L"\\link\\data.txt");
    req.items.push_back(open_through);

    ProtocolReparsePoint::Req::Item attributes_through;
    attributes_through.action = "attributes";
    attributes_through.path = appbox::WideToUTF8(folder + L"\\link\\data.txt");
    req.items.push_back(attributes_through);

    ProtocolReparsePoint::Req::Item link;
    link.action = "attributes";
    link.path = appbox::WideToUTF8(folder + L"\\link");
    req.items.push_back(link);

    const auto rsp = ProbeReparsePoint.Call(req, GetCWD(), config).get<ProtocolReparsePoint::Rsp>();
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(3));

    /* The view refuses the call instead of forwarding it to the layer. */
    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_REPARSE_POINT_NOT_RESOLVED));
    EXPECT_TRUE(rsp.items[0].text.empty());

    EXPECT_EQ(rsp.items[1].attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_NE(rsp.items[1].code, static_cast<DWORD>(ERROR_SUCCESS));

    /* The junction itself is still an entry of the view. */
    EXPECT_NE(rsp.items[2].attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_NE(rsp.items[2].attributes & FILE_ATTRIBUTE_REPARSE_POINT, static_cast<DWORD>(0));

    ASSERT_TRUE(tree.Verify());
}
