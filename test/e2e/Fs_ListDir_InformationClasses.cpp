#include "probe/ListDirNt.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/NtDirectoryQuery.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_ListClasses";

/** Names the merged view of the folder holds, in the order the probe sorts them. */
const std::vector<std::string> kExpectedNames = { "host.txt", "visible.txt" };

/** The information classes which carry the name of an entry. */
const char* const kClasses[] = {
    "FileDirectoryInformation",       "FileFullDirectoryInformation",       "FileBothDirectoryInformation",
    "FileNamesInformation",           "FileIdBothDirectoryInformation",     "FileIdFullDirectoryInformation",
    "FileIdExtdDirectoryInformation", "FileIdExtdBothDirectoryInformation", "FileIdGlobalTxDirectoryInformation",
};

/**
 * @brief Assert that an enumeration reported the merged view of the folder.
 * @param[in] names Names the enumeration reported.
 */
static void ExpectMergedEntries(const std::vector<std::string>& names)
{
    EXPECT_EQ(names, kExpectedNames);
}

/**
 * @brief Whether the file system of a path refuses an information class.
 *
 * The shared enumeration runs in the process which calls it, which is the
 * process of this executable and not a process of the sandbox, so the answer is
 * the one the file system gives for the path itself and not the merged view of
 * the layers.
 *
 * @param[in] path Path of the directory.
 * @param[in] info_class Information class of the query.
 * @param[in] extended true to query with `NtQueryDirectoryFileEx`.
 * @return true when the file system refuses the class with
 *         `STATUS_INVALID_INFO_CLASS`.
 */
static bool FileSystemRefusesClass(const std::filesystem::path& path, const char* info_class, bool extended)
{
    NtDirectoryQuery query;
    query.path = path.wstring();
    query.info_class = info_class;
    query.extended = extended;

    return QueryNtDirectory(query).status == STATUS_INVALID_INFO_CLASS;
}

/**
 * Condition:
 * 1. The lower layer holds `visible.txt` and `hidden.txt`, the state of the
 *    sandbox holds a whiteout marker for `hidden.txt` and the folder of the
 *    host holds `host.txt`.
 * 2. The sandboxed process enumerates the folder with both NT entry points and
 *    with every information class which carries the name of an entry.
 *
 * Expected:
 * 1. Every enumeration reports the same merged view: the file of the lower
 *    layer, the file of the host and no marker.
 * 2. The file the whiteout hides is not reported, whatever class the caller
 *    uses, so the merge and the layout of the class agree with the file system.
 * 3. The view keeps the smaller feature set of its layers: the state, the
 *    resource and the host layer of the view may live on file systems which do
 *    not support the same classes, and a class a layer refuses is refused by
 *    the whole view as well. A caller of a class the view refuses, which is one
 *    the file systems of the machine do answer, would never see the folder, so
 *    a refusal of the view has to be the refusal of a layer.
 * 4. The host folder and the lower layer are unchanged.
 */
TEST_F(E2E_Fs, ListDir_InformationClasses)
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

    /*
     * The paths the view enumerates: the state layer, the resource layer and
     * the folder of the host. The two roots of the sandbox are directories of
     * the machine the case runs on, while the folder of the host is a folder of
     * the system drive, so the layers of the view may live on different file
     * systems.
     */
    const std::vector<std::filesystem::path> layerPaths = {
        GetCWD() / L"data" / L"filesystem" / GetKnownFolderPath(L"#USERPROFILE#", true) / kFolderName,
        GetCWD() / L"app" / L"filesystem" / L"#USERPROFILE#" / kFolderName,
        host.Get(),
    };

    for (const char* cls : kClasses)
    {
        for (const bool extended : { false, true })
        {
            SCOPED_TRACE(std::string(cls) + (extended ? " (extended)" : " (plain)"));

            ProtocolListDirNt::Req req;
            req.path = appbox::WideToUTF8(folder);
            req.extended = extended;
            req.info_class = cls;

            auto rsp = Probe(GetCWD(), config).Call<ProtocolListDirNt>(req);

            if (rsp.status == STATUS_INVALID_INFO_CLASS)
            {
                /*
                 * The view refused the class because a layer it is composed of
                 * cannot answer it, which is the smaller feature set of the
                 * layers. At least one of the paths the view enumerated has to
                 * refuse the class on its own as well: a view which refused a
                 * class its layers support would hide the folder from the
                 * caller for a class the file systems of the machine do answer.
                 */
                bool refused = false;
                for (const auto& layer : layerPaths)
                {
                    if (FileSystemRefusesClass(layer, cls, extended))
                    {
                        refused = true;
                        break;
                    }
                }

                EXPECT_TRUE(refused) << "no layer of the view refuses the class " << cls;
                continue;
            }

            ASSERT_EQ(rsp.status, 0L);
            ExpectMergedEntries(rsp.names);
        }
    }

    /* The host folder was not touched. */
    EXPECT_TRUE(host.FileExists(L"host.txt"));

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
