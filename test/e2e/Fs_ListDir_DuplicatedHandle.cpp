#include "probe/ListDirNt.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_ListDuplicated";

/** Names the merged view of the folder holds, in the order the probe sorts them. */
const std::vector<std::string> kExpectedNames = { "host.txt", "visible.txt" };

/** The classes the case enumerates the folder with. */
const char* const kClasses[] = {
    "FileFullDirectoryInformation",
    "FileIdBothDirectoryInformation",
};

/**
 * Condition:
 * 1. The lower layer holds `visible.txt` and `hidden.txt`, the state of the
 *    sandbox holds a whiteout marker for `hidden.txt` and the folder of the
 *    host holds `host.txt`, so the layer a handle is opened with holds fewer
 *    entries than the view.
 * 2. The sandboxed process duplicates the handle of the folder and closes the
 *    original, so the handle it enumerates carries no record of an open the
 *    sandbox performed: it is the handle a process inherits or duplicates from
 *    another process.
 * 3. The enumeration runs through both NT entry points, over a handle of
 *    `NtOpenFile` and over a handle of `CreateFileW`.
 *
 * Expected:
 * 1. Every enumeration reports the merged view, so the view answers through a
 *    handle it did not open like it answers through one it opened.
 * 2. The file the whiteout hides is not reported and no marker is reported.
 * 3. The host folder and the lower layer are unchanged.
 */
TEST_F(E2E_Fs, ListDir_DuplicatedHandle)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"host.txt", "host"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {
            FsDir(L"filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true), {
                FsDir(kFolderName, {
                    FsFile(L"hidden.txt.$APPBOX_DELETE$", "")
                })
            })
        }),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"visible.txt", "packed"),
                    FsFile(L"hidden.txt", "packed")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto folder = GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;

    for (const char* cls : kClasses)
    {
        for (const bool extended : { false, true })
        {
            for (const bool create_file : { false, true })
            {
                SCOPED_TRACE(std::string(cls) + (extended ? " (extended)" : " (plain)") +
                             (create_file ? " (CreateFileW)" : " (NtOpenFile)"));

                ProtocolListDirNt::Req req;
                req.path = appbox::WideToUTF8(folder);
                req.extended = extended;
                req.info_class = cls;
                req.create_file = create_file;
                req.duplicate = true;

                auto rsp = ProbeListDirNt.Call(req, GetCWD(), config).get<ProtocolListDirNt::Rsp>();
                ASSERT_EQ(rsp.status, 0L);
                EXPECT_EQ(rsp.names, kExpectedNames);
            }
        }
    }

    /* The host folder was not touched. */
    EXPECT_TRUE(host.FileExists(L"host.txt"));

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
