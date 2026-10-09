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
constexpr wchar_t kFolderName[] = L"AppBoxTest_ListUnsupported";

/**
 * @brief The information classes which carry no name of an entry.
 *
 * The merge of the layers cannot read an entry with a class of that kind, so
 * the view refuses it instead of answering it from one layer. The file system
 * refuses the same class for a directory of a volume with
 * `STATUS_INVALID_INFO_CLASS`, so the status a case receives pins the refusal
 * of the view.
 */
const char* const kClasses[] = {
    "FileObjectIdInformation",
    "FileReparsePointInformation",
};

/**
 * Condition:
 * 1. The lower layer holds `visible.txt` and `hidden.txt`, the state of the
 *    sandbox holds a whiteout marker for `hidden.txt` and the folder of the
 *    host holds `host.txt`, so the merged view of the folder holds two entries
 *    while the layer a handle is opened with holds fewer.
 * 2. The sandboxed process enumerates the folder with an information class
 *    which carries no name of an entry, through both NT entry points and with
 *    both ways a directory handle reaches the sandbox.
 *
 * Expected:
 * 1. Every call is refused by the view with `STATUS_NOT_SUPPORTED` and reports
 *    no entry: the view never answers with the content of the single layer the
 *    handle was opened with, which would show the entries a whiteout hides and
 *    the markers of the view themselves.
 * 2. The host folder and the lower layer are unchanged.
 */
TEST_F(E2E_Fs, ListDir_UnsupportedClass)
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

                auto rsp = ProbeListDirNt.Call(req, GetCWD(), config).get<ProtocolListDirNt::Rsp>();

                EXPECT_EQ(rsp.status, STATUS_NOT_SUPPORTED);
                EXPECT_TRUE(rsp.names.empty());
            }
        }
    }

    /* The host folder was not touched. */
    EXPECT_TRUE(host.FileExists(L"host.txt"));

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
