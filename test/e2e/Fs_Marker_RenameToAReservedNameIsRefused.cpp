#include "probe/ReadFileFull.hpp"
#include "probe/SetInformationFile.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
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
constexpr wchar_t kFolderName[] = L"AppBoxTest_MarkerRename";

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

/** @brief Class of the call of one name the case asks for. */
using Action = ProtocolSetInformationFile::Req::Item::Action;

/** @brief Access the source of one name is opened with. */
using Access = ProtocolSetInformationFile::Req::Item::Access;

/**
 * @brief One name the case asks the sandbox to move or link an entry to.
 */
struct SetNameAction
{
    /** Class of the call, see Action. */
    Action action = Action::Rename;

    /** Name the object is moved to or linked at. */
    std::wstring target;

    /** Access the source is opened with, see Access. */
    Access access = Access::Delete;
};

/**
 * @brief Perform the actions of the case with one probe call.
 * @param[in] actions Actions to perform, in order.
 * @param[in] cwd Working directory of the case.
 * @param[in] config Configuration of the case.
 * @return The answer of the sandboxed process, one item per action.
 */
ProtocolSetInformationFile::Rsp SetNames(const std::vector<SetNameAction>& actions, const std::filesystem::path& cwd,
                                         appbox::LauncherConfig& config)
{
    ProtocolSetInformationFile::Req req;
    for (const auto& action : actions)
    {
        ProtocolSetInformationFile::Req::Item item;
        item.action = action.action;
        item.source = appbox::WideToUTF8(FolderPath() + L"\\source.txt");
        item.target = appbox::WideToUTF8(action.target);
        item.access = action.access;
        req.items.push_back(item);
    }

    return Probe(cwd, config).Call<ProtocolSetInformationFile>(req);
}

} // namespace

/**
 * Condition:
 * 1. The lower layer holds `source.txt` in the folder of the case, and the
 *    folder isolates with `Write Copy`.
 * 2. The sandboxed process renames the file onto the name of a marker, onto a
 *    name below a marker, and links it at the name of a marker.
 *
 * Expected:
 * 1. Every call is refused: the name of an entry which carries a reserved name
 *    is refused with `Object Name Invalid`, and a name below one is refused
 *    with `Object Path Not Found`.
 * 2. The file keeps its name and its content, and the overlay records no
 *    marker.
 */
TEST_F(E2E_Fs, Marker_RenameToAReservedNameIsRefused)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"source.txt", "packed")
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

    const std::vector<SetNameAction> actions = {
        { Action::Rename, FolderPath() + L"\\data.txt.$APPBOX_DELETE$",         Access::Delete },
        { Action::Rename, FolderPath() + L"\\hidden.$APPBOX_DELETE$\\file.txt", Access::Delete },
        { Action::Link,   FolderPath() + L"\\linked.$APPBOX_DELETE$",           Access::Read   },
    };

    const auto rsp = SetNames(actions, GetCWD(), config);
    ASSERT_EQ(rsp.items.size(), actions.size());

    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_OBJECT_NAME_INVALID));
    EXPECT_EQ(rsp.items[1].status, static_cast<long>(STATUS_OBJECT_PATH_NOT_FOUND));
    EXPECT_EQ(rsp.items[2].status, static_cast<long>(STATUS_OBJECT_NAME_INVALID));

    /* The source keeps its name and its content. */
    {
        ProtocolReadFileFull::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\source.txt");

        const auto read = Probe(GetCWD(), config).Call<ProtocolReadFileFull>(req);
        ASSERT_EQ(read.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(read.data, "packed");
    }

    /* The overlay records no marker. */
    {
        const auto overlay = FolderInOverlay(GetCWDString());
        EXPECT_FALSE(std::filesystem::exists(overlay + L"\\data.txt.$APPBOX_DELETE$"));
        EXPECT_FALSE(std::filesystem::exists(overlay + L"\\linked.$APPBOX_DELETE$"));
    }

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
