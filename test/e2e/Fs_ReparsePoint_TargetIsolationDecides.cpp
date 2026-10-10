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
constexpr wchar_t kFolderName[] = L"AppBoxTest_ReparseTarget";

/**
 * Condition:
 * 1. The folder of the case holds two junctions of the host: `link_hidden`
 *    names the folder `hidden` and `link_visible` names the folder `visible`.
 *    The isolation mode of the folder is `Write Copy`, while the folder
 *    `hidden` is hidden with `Whiteout`.
 * 2. The sandboxed process queries the junction with both entry points of an
 *    attribute query, reads the data of the junction back, reads the file of
 *    the target directly and through the junction, and queries the attributes
 *    of the hidden folder.
 *
 * Expected:
 * 1. The read through `link_hidden` reports the file as missing, because the
 *    view resolves the junction and the isolation of the target hides it: the
 *    answer of the call is the answer of the folder the junction names and not
 *    the answer of the folder the caller spelled.
 * 2. The read through `link_visible` reports the content of the target, so the
 *    view reaches an entry through a junction, and the data of the junction
 *    names the path of the view.
 * 3. The attributes of the hidden folder report `File Not Found`, while the
 *    junction itself is an entry of its own folder which carries the attribute
 *    of a reparse point, which both entry points of the attribute query report
 *    without following it.
 * 4. The files of the host are untouched.
 */
TEST_F(E2E_Fs, ReparsePoint_TargetIsolationDecides)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"hidden\\data.txt", "hidden"));
    ASSERT_TRUE(host.WriteFile(L"visible\\data.txt", "visible"));
    ASSERT_TRUE(host.CreateJunction(L"link_hidden", (host.Get() / L"hidden").wstring()));
    ASSERT_TRUE(host.CreateJunction(L"link_visible", (host.Get() / L"visible").wstring()));

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
    const std::wstring key = L"#USERPROFILE#\\" + std::wstring(kFolderName);
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(),
        {
            { key,               appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::WriteCopy },
            { key + L"\\hidden", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Whiteout  }
    }));

    ProtocolReparsePoint::Req req;

    const auto add = [&req](const char* action, const std::wstring& path) {
        ProtocolReparsePoint::Req::Item item;
        item.action = action;
        item.path = appbox::WideToUTF8(path);
        req.items.push_back(item);
    };

    /* 0: the junction is an entry of the view which carries a reparse point. */
    add("attributes", folder + L"\\link_visible");
    /* 1: the second entry point of an attribute query reports the same entry. */
    add("full_attributes", folder + L"\\link_visible");
    /* 2: the data of the junction names the target. */
    add("read", folder + L"\\link_visible");
    /* 3: the target of the junction is visible itself. */
    add("attributes", folder + L"\\visible");
    /* 4: the file of the target is readable. */
    add("read_text", folder + L"\\visible\\data.txt");
    /* 5: the view resolves the junction and reaches the same file. */
    add("read_text", folder + L"\\link_visible\\data.txt");
    /* 6: the view resolves the junction whose target the isolation hides. */
    add("read_text", folder + L"\\link_hidden\\data.txt");
    /* 7: the hidden folder is not visible itself either. */
    add("attributes", folder + L"\\hidden");

    const auto rsp = ProbeReparsePoint.Call(req, GetCWD(), config).get<ProtocolReparsePoint::Rsp>();
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(8));

    EXPECT_NE(rsp.items[0].attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_NE(rsp.items[0].attributes & FILE_ATTRIBUTE_REPARSE_POINT, static_cast<DWORD>(0));

    EXPECT_NE(rsp.items[1].attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_NE(rsp.items[1].attributes & FILE_ATTRIBUTE_REPARSE_POINT, static_cast<DWORD>(0));

    EXPECT_NE(rsp.items[2].substitute.find("\\visible"), std::string::npos);

    EXPECT_NE(rsp.items[3].attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_NE(rsp.items[3].attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(0));

    EXPECT_EQ(rsp.items[4].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[4].text, "visible");

    EXPECT_EQ(rsp.items[5].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[5].text, "visible");

    EXPECT_NE(rsp.items[6].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_TRUE(rsp.items[6].text.empty());

    EXPECT_EQ(rsp.items[7].attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));

    /* The host files were not touched. */
    EXPECT_TRUE(host.FileExists(L"hidden\\data.txt"));
    EXPECT_TRUE(host.FileExists(L"visible\\data.txt"));
    ASSERT_TRUE(tree.Verify());
}
