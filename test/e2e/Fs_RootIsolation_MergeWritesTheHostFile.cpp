#include "probe/WriteFile.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <string>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_RootMerge";

/**
 * @brief Get the path of the folder of the case inside the view.
 * @return Path of the folder.
 */
std::wstring FolderPath()
{
    return GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
}

} // namespace

/**
 * Condition:
 * 1. The isolation file lists the root of the view only, with `Merge`, so the
 *    folder of the case carries no entry of its own.
 * 2. The host filesystem holds the file `data.txt` of the folder.
 * 3. The sandboxed process opens the file for writing.
 *
 * Expected:
 * 1. The mode of the root reaches the write: the modification is applied to the
 *    file of the host filesystem, because the folder inherits the `Merge` of
 *    the root.
 * 2. The file of the host carries the new content.
 */
TEST_F(E2E_Fs, RootIsolation_MergeWritesTheFileOfTheHost)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"data.txt", "host"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {})
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    /* The root of the view is the entry without a path. */
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Merge }
    }));

    /* The write reaches the file of the host filesystem. */
    {
        ProtocolWriteFile::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\data.txt");
        req.Data = "written";

        const auto rsp = ProbeWriteFile.Call(req, GetCWD(), config).get<ProtocolWriteFile::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "written");
    }

    /* The host file carries the new content. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull((host.Get() / L"data.txt").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "written");
    }

    ASSERT_TRUE(tree.Verify());
}
