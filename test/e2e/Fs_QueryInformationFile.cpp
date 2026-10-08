#include "probe/QueryInformationFile.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name of the folder of these cases below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_QueryInformationFile";

/**
 * @brief Get the path of the folder of the cases inside the view.
 * @return Path of the folder.
 */
std::wstring Folder()
{
    return GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
}

/**
 * @brief Get the name a name class reports for a file of the folder.
 *
 * The file system names an object relative to the root of its volume, so the
 * name of a file of the view is the path of the view without its drive.
 *
 * @param[in] file Name of the file below the folder.
 * @return The name the file system reports for the file.
 */
std::wstring ExpectedName(const std::wstring& file)
{
    return (Folder() + L"\\" + file).substr(2);
}

/**
 * @brief One file the case asks the sandbox to name.
 */
struct QueryAction
{
    std::wstring  file;
    std::wstring  mode = L"read";
    unsigned long length = 0;
};

/**
 * @brief Ask the name classes of several files with one probe call.
 *
 * @param[in] actions Files to ask, in the order they are opened.
 * @param[in] cwd Working directory of the case.
 * @param[in] config Configuration of the case.
 * @return The answer of the sandboxed process.
 */
ProtocolQueryInformationFile::Rsp QueryInformationFile(const std::vector<QueryAction>& actions,
                                                       const std::filesystem::path& cwd, appbox::LauncherConfig& config)
{
    ProtocolQueryInformationFile::Req req;
    for (const auto& action : actions)
    {
        ProtocolQueryInformationFile::Req::Item item;
        item.path = appbox::WideToUTF8(action.file);
        item.mode = appbox::WideToUTF8(action.mode);
        item.length = action.length;
        req.items.push_back(item);
    }

    return ProbeQueryInformationFile.Call(req, cwd, config).get<ProtocolQueryInformationFile::Rsp>();
}

} // namespace

/**
 * Condition:
 * 1. The folder is isolated with `Write Copy` and holds the packed file
 *    `data.txt`; the host holds the file `host.txt` of the same folder.
 * 2. The sandboxed process opens the packed file for reading, then opens it
 *    for writing, which copies it into the overlay, and opens the file of the
 *    host for reading. It asks `FileNameInformation`,
 *    `FileNormalizedNameInformation` and `FileAllInformation` for every one of
 *    them.
 *
 * Expected:
 * 1. Every class reports the name of the view for every handle: the file of
 *    the lower layer, the copy of the overlay the write opened and the file of
 *    the host, so the layout of the sandbox never reaches the application.
 * 2. The overlay holds the copy the write opened.
 * 3. The host file and the resources of the application are untouched.
 */
TEST_F(E2E_Fs, QueryInformationFile_ViewPath)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"host.txt", "host"));

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

    const auto rsp = QueryInformationFile(
        {
            { Folder() + L"\\data.txt", L"read"  },
            { Folder() + L"\\data.txt", L"write" },
            { Folder() + L"\\host.txt", L"read"  },
    },
        GetCWD(), config);
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(3));

    const std::string packed_name = appbox::WideToUTF8(ExpectedName(L"data.txt"));
    const std::string host_name = appbox::WideToUTF8(ExpectedName(L"host.txt"));

    /* The handle of the lower layer and the handle of the copy of the overlay
     * are both named after the view. */
    for (size_t i = 0; i < 2; ++i)
    {
        EXPECT_EQ(rsp.items[i].status, static_cast<long>(STATUS_SUCCESS)) << "item " << i;
        EXPECT_EQ(rsp.items[i].nameStatus, static_cast<long>(STATUS_SUCCESS)) << "item " << i;
        EXPECT_EQ(rsp.items[i].normalizedStatus, static_cast<long>(STATUS_SUCCESS)) << "item " << i;
        EXPECT_EQ(rsp.items[i].allStatus, static_cast<long>(STATUS_SUCCESS)) << "item " << i;
        EXPECT_EQ(rsp.items[i].name, packed_name) << "item " << i;
        EXPECT_EQ(rsp.items[i].normalized, packed_name) << "item " << i;
        EXPECT_EQ(rsp.items[i].all, packed_name) << "item " << i;
    }

    /* The file of the host is named after the view as well, which is the path
     * the host filesystem holds for it. */
    EXPECT_EQ(rsp.items[2].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[2].nameStatus, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[2].normalizedStatus, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[2].allStatus, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[2].name, host_name);
    EXPECT_EQ(rsp.items[2].normalized, host_name);
    EXPECT_EQ(rsp.items[2].all, host_name);

    /* The write copied the file of the lower layer into the overlay. */
    {
        const auto overlay = GetCWDString() + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) +
                             L"\\" + kFolderName + L"\\data.txt";
        EXPECT_TRUE(std::filesystem::exists(overlay));
    }

    /* The host file and the resources of the application are untouched. */
    EXPECT_TRUE(host.FileExists(L"host.txt"));
    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. The folder is isolated with `Write Copy` and holds the packed file
 *    `data.txt` of the lower layer only.
 * 2. The sandboxed process asks the name classes of the file with a buffer
 *    which is large enough for the name of the view and too small for the name
 *    of the layer the handle denotes.
 *
 * Expected:
 * 1. Every class reports the name of the view and succeeds, so a caller which
 *    sized its buffer after the view is never told that the buffer is too
 *    small, which is what the path of the layer would ask for.
 * 2. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, QueryInformationFile_SmallBuffer)
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

    const std::wstring expected = ExpectedName(L"data.txt");

    /* The size of the record of `FileAllInformation` for the name of the view. */
    const unsigned long length = static_cast<unsigned long>(offsetof(FILE_ALL_INFORMATION, NameInformation) +
                                                            sizeof(ULONG) + expected.size() * sizeof(wchar_t));

    const auto rsp = QueryInformationFile(
        {
            { Folder() + L"\\data.txt", L"read", length }
    },
        GetCWD(), config);
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(1));

    const std::string name = appbox::WideToUTF8(expected);
    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[0].nameStatus, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[0].normalizedStatus, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[0].allStatus, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[0].name, name);
    EXPECT_EQ(rsp.items[0].normalized, name);
    EXPECT_EQ(rsp.items[0].all, name);

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
