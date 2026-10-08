#include "probe/QueryFullAttributes.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/RealFsFolder.hpp"
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
constexpr wchar_t kFolderName[] = L"AppBoxTest_QueryFullAttributes";

/** Path of the folder of this case inside the view. */
std::wstring Folder()
{
    return GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
}

/**
 * @brief Query the attributes of several paths with one probe call.
 *
 * The probe answers one item per path in the order of the request, so a case
 * pins every question with a single call into the sandbox.
 *
 * @param[in] paths Paths to query, in view form.
 * @param[in] cwd Working directory of the case.
 * @param[in] config Configuration of the case.
 * @return The answer of the sandboxed process.
 */
ProtocolQueryFullAttributes::Rsp QueryFullAttributes(const std::vector<std::wstring>& paths,
                                                     const std::filesystem::path& cwd, appbox::LauncherConfig& config)
{
    ProtocolQueryFullAttributes::Req req;
    for (const auto& path : paths)
    {
        req.paths.push_back(appbox::WideToUTF8(path));
    }

    return ProbeQueryFullAttributes.Call(req, cwd, config).get<ProtocolQueryFullAttributes::Rsp>();
}

} // namespace

/**
 * Condition:
 * 1. The file exists in the lower layer of the resources only.
 * 2. The sandboxed process queries its full attributes with
 *    `NtQueryFullAttributesFile`.
 *
 * Expected:
 * 1. The query succeeds and reports a regular file with the size of the packed
 *    content, so the query hit the layer of the view instead of the raw path.
 * 2. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, QueryFullAttributes_LowerLayer)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"data.txt", "hello1")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto rsp = QueryFullAttributes({ Folder() + L"\\data.txt" }, GetCWD(), config);
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(1));

    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_NE(rsp.items[0].attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY));
    EXPECT_EQ(rsp.items[0].size, static_cast<long long>(6));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. The folder exists in the lower layer of the resources, the queried file
 *    does not.
 * 2. The sandboxed process queries a file of the folder and a file of a folder
 *    which does not exist.
 *
 * Expected:
 * 1. The query of the missing file fails with `File Not Found`, because the
 *    folder of the view does not hold it.
 * 2. The query of the file below a missing folder fails with `Path Not Found`.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, QueryFullAttributes_NonExists)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"other.txt", "hello1")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto rsp = QueryFullAttributes(
        {
            Folder() + L"\\data.txt",
            Folder() + L"\\missing\\data.txt",
        },
        GetCWD(), config);
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(2));

    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_OBJECT_NAME_NOT_FOUND));
    EXPECT_EQ(rsp.items[1].status, static_cast<long>(STATUS_OBJECT_PATH_NOT_FOUND));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. The folder exists in a lower layer and in the host, and it is isolated
 *    with `Full`, so the host folder is masked; one file of the lower layer is
 *    isolated with `Whiteout`, so it is masked as well.
 * 2. The sandboxed process queries the attributes of the packed file, of the
 *    host file and of the hidden packed file.
 *
 * Expected:
 * 1. The packed file of the visible folder is reported, so a query which
 *    carries a name follows the view.
 * 2. The host file is not reported, because `Full` hides the host folder.
 * 3. The hidden packed file is not reported, because `Whiteout` hides it.
 * 4. The host folder is unchanged and the resources of the application are
 *    untouched.
 */
TEST_F(E2E_Fs, QueryFullAttributes_IsolationHidden)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"host.txt", "host"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"packed.txt", "packed"),
                    FsFile(L"whiteout.txt", "packed-whiteout")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName),                     appbox::FilesystemEntryKind::Directory,
                       appbox::FilesystemIsolation::Full                                                                                                                  },
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName) + L"\\whiteout.txt",
                       appbox::FilesystemEntryKind::File,                                                                           appbox::FilesystemIsolation::Whiteout }
    }));

    const auto rsp = QueryFullAttributes(
        {
            Folder() + L"\\packed.txt",
            Folder() + L"\\host.txt",
            Folder() + L"\\whiteout.txt",
        },
        GetCWD(), config);
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(3));

    /* The packed file of the visible folder is reported. */
    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_NE(rsp.items[0].attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY));
    EXPECT_EQ(rsp.items[0].size, static_cast<long long>(6));

    /* The host file and the hidden packed file are not. */
    EXPECT_EQ(rsp.items[1].status, static_cast<long>(STATUS_OBJECT_NAME_NOT_FOUND));
    EXPECT_EQ(rsp.items[2].status, static_cast<long>(STATUS_OBJECT_NAME_NOT_FOUND));

    /* The host folder was not touched. */
    EXPECT_TRUE(host.FileExists(L"host.txt"));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
