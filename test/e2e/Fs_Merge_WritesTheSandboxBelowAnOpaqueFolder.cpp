#include "probe/WriteFile.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/ReadFileFull.hpp"
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
constexpr wchar_t kFolderName[] = L"AppBoxTest_MergeOpaque";

/** Name of the marker which makes a folder of the overlay opaque. */
constexpr wchar_t kOpaqueMarkerName[] = L".$APPBOX_OPAQUE$";

/**
 * @brief Get the path of the folder of the case inside the view.
 * @return Path of the folder.
 */
std::wstring FolderPath()
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
 * 1. The overlay holds the folder `data.txt` is created in together with the
 *    marker which makes it opaque, so the folder of the host filesystem stays
 *    hidden; the host holds `data.txt` nevertheless.
 * 2. The folder is isolated with `Merge`.
 * 3. The sandboxed process creates `data.txt` with `CREATE_ALWAYS`.
 *
 * Expected:
 * 1. The file is created in the overlay, because a sandbox layer holds the
 *    path: the opaque marker is part of the state of the sandbox, so the entry
 *    belongs to it even while the host holds a file of the same name.
 * 2. The read back reports the new content, and the file of the host
 *    filesystem keeps its own content.
 */
TEST_F(E2E_Fs, Merge_WritesTheSandboxBelowAnOpaqueFolder)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"data.txt", "host"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {
            FsDir(L"filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true), {
                FsDir(kFolderName, {
                    FsFile(kOpaqueMarkerName, "")
                })
            })
        }),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {})
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
                       appbox::FilesystemIsolation::Merge }
    }));

    /* The entry a sandbox layer holds is written inside the sandbox. */
    {
        ProtocolWriteFile::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\data.txt");
        req.Data = "written";
        req.dwCreationDisposition = CREATE_ALWAYS;

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolWriteFile>(req);
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "written");
    }

    /* The copy of the overlay carries the new content. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull(FolderInOverlay(GetCWDString()) + L"\\data.txt", data),
                  static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "written");
    }

    /* The file of the host filesystem was not touched. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull((host.Get() / L"data.txt").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "host");
    }

    ASSERT_TRUE(tree.Verify());
}
