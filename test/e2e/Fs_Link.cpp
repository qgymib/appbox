#include "probe/SetInformationFile.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_Link";

/**
 * @brief Get the path of the folder of the case inside the view.
 * @return Path of the folder.
 */
std::wstring Folder()
{
    return GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
}

/**
 * @brief Get the path of the folder of the case inside the overlay.
 * @param[in] cwd Working directory of the case.
 * @return Path of the folder in the overlay.
 */
std::wstring FolderInOverlay(const std::wstring& cwd)
{
    return cwd + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) + L"\\" + kFolderName;
}

} // namespace

/**
 * Condition:
 * 1. The file exists in the lower layer of the resources only and the folder
 *    is isolated with `Write Copy`.
 * 2. The sandboxed process links the file to a second name of the folder. The
 *    source is opened with an access which does not modify it, so the entry of
 *    the lower layer is not copied up.
 *
 * Expected:
 * 1. The link succeeds and is an entry of the overlay which carries the
 *    content of the packed file.
 * 2. The source stays where it is: the overlay holds no copy of it and no
 *    whiteout marker hides it, which is what tells a link from a rename.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, Link_LowerLayer)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"data.txt", "packed")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
                       appbox::FilesystemIsolation::WriteCopy }
    }));

    {
        ProtocolSetInformationFile::Req       req;
        ProtocolSetInformationFile::Req::Item item;
        item.action = "link";
        item.source = appbox::WideToUTF8(Folder() + L"\\data.txt");
        item.target = appbox::WideToUTF8(Folder() + L"\\linked.txt");
        item.access = "read";
        req.items.push_back(item);

        const auto rsp = ProbeSetInformationFile.Call(req, GetCWD(), config).get<ProtocolSetInformationFile::Rsp>();
        ASSERT_EQ(rsp.items.size(), static_cast<size_t>(1));
        EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_SUCCESS));
    }

    /* The new name is an entry of the overlay which carries the packed content. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull(FolderInOverlay(GetCWDString()) + L"\\linked.txt", data),
                  static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "packed");
    }

    /* The source was neither carried into the overlay nor hidden. */
    EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\data.txt"));
    EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\data.txt.$APPBOX_DELETE$"));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
