#include "probe/CreateDirectoryW.hpp"
#include "probe/CreateFileW.hpp"
#include "probe/DeleteFileW.hpp"
#include "probe/QueryAttributes.hpp"
#include "probe/RemoveDirectory.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_Directory";

/**
 * @brief Get the path of the directory the case creates in the view.
 * @return Path of the directory in the view.
 */
std::wstring CreatedDirectory()
{
    return GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName + L"\\created";
}

/**
 * @brief Get the path of the directory of the case in the overlay.
 * @param[in] cwd Working directory of the case.
 * @return Path of the directory in the overlay.
 */
std::wstring CreatedDirectoryInOverlay(const std::wstring& cwd)
{
    return cwd + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) + L"\\" + kFolderName +
           L"\\created";
}

} // namespace

/**
 * Condition:
 * 1. The sandboxed process creates a directory below a folder which only the
 *    host holds, asks for its attributes, creates a file inside it and removes
 *    the directory while the file is still there.
 * 2. The layer `#USERPROFILE#` of the case is pinned to `Write Copy`, so the
 *    directory is created in the sandbox and the case pins its own rule
 *    instead of the default of the view.
 * 3. It removes the file and removes the directory again.
 *
 * Expected:
 * 1. The directory is created inside the sandbox: the view reports it as a
 *    directory, the overlay holds it and the host folder does not gain it.
 * 2. The removal of a directory which still holds a visible entry is refused
 *    with `Directory Not Empty`; after the file was removed the removal
 *    succeeds, and the directory is gone from the view and from the overlay.
 * 3. The host folder is unchanged.
 */
TEST_F(E2E_Fs, Directory_CreateAndDelete)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {})
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    /*
     * The layer of the case is pinned to `Write Copy`, which is the mode a
     * path had before the default of the view became `Merge`: the case pins
     * the rule it is about and never reaches the host filesystem.
     */
    ASSERT_TRUE(WriteFsIsolationFile(GetCWD(), {
                                                   { L"#USERPROFILE#", appbox::FilesystemEntryKind::Directory,
                                                    appbox::FilesystemIsolation::WriteCopy }
    }));

    const auto created = CreatedDirectory();
    const auto file = created + L"\\data.txt";

    /* The sandbox creates the directory. */
    {
        ProtocolCreateDirectoryW::Req req;
        req.PathName = appbox::WideToUTF8(created);

        const auto rsp = ProbeCreateDirectoryW.Call(req, GetCWD(), config).get<ProtocolCreateDirectoryW::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    /* The view reports it as a directory. */
    {
        ProtocolQueryAttributes::Req req;
        req.FileName = appbox::WideToUTF8(created);

        const auto rsp = ProbeQueryAttributes.Call(req, GetCWD(), config).get<ProtocolQueryAttributes::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_NE(rsp.attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(0));
    }

    /* It landed in the overlay, and the host folder stays empty. */
    EXPECT_TRUE(std::filesystem::exists(CreatedDirectoryInOverlay(GetCWDString())));
    EXPECT_FALSE(host.FileExists(L"created"));

    /* An entry the sandbox creates inside the directory is visible as well. */
    {
        ProtocolCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(file);
        req.dwDesiredAccess = GENERIC_WRITE;
        req.dwCreationDisposition = CREATE_NEW;

        const auto rsp = ProbeCreateFileW.Call(req, GetCWD(), config).get<ProtocolCreateFileW::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    /* A directory which still holds a visible entry is not removed. */
    {
        ProtocolRemoveDirectory::Req req;
        req.PathName = appbox::WideToUTF8(created);

        const auto rsp = ProbeRemoveDirectory.Call(req, GetCWD(), config).get<ProtocolRemoveDirectory::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_DIR_NOT_EMPTY));
    }

    /* After the entry was removed, the directory is removed as well. */
    {
        ProtocolDeleteFileW::Req req;
        req.FileName = appbox::WideToUTF8(file);

        const auto rsp = ProbeDeleteFileW.Call(req, GetCWD(), config).get<ProtocolDeleteFileW::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }
    {
        ProtocolRemoveDirectory::Req req;
        req.PathName = appbox::WideToUTF8(created);

        const auto rsp = ProbeRemoveDirectory.Call(req, GetCWD(), config).get<ProtocolRemoveDirectory::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    /* The directory is gone from the view and from the overlay. */
    {
        ProtocolQueryAttributes::Req req;
        req.FileName = appbox::WideToUTF8(created);

        const auto rsp = ProbeQueryAttributes.Call(req, GetCWD(), config).get<ProtocolQueryAttributes::Rsp>();
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }
    EXPECT_FALSE(std::filesystem::exists(CreatedDirectoryInOverlay(GetCWDString())));

    /* The host folder is unchanged. */
    EXPECT_FALSE(host.FileExists(L"created"));
    ASSERT_TRUE(tree.Verify());
}
