#include "probe/SetInformationFile.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/ReadFileFull.hpp"
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

/** Name of the folder of these cases below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_Rename";

/**
 * @brief Get the path of the folder of the cases inside the view.
 * @return Path of the folder.
 */
std::wstring Folder()
{
    return GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
}

/**
 * @brief Get the path of the folder of the cases inside the overlay.
 * @param[in] cwd Working directory of the case.
 * @return Path of the folder in the overlay.
 */
std::wstring FolderInOverlay(const std::wstring& cwd)
{
    return cwd + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) + L"\\" + kFolderName;
}

/**
 * @brief One rename the case asks the sandbox to perform.
 */
struct RenameAction
{
    std::wstring source;
    std::wstring target;
    std::wstring rootDirectory;
    bool         replaceIfExists = false;
};

/**
 * @brief Perform renames with one probe call.
 *
 * The probe answers one item per action in the order of the request, so a case
 * pins every question with a single call into the sandbox.
 *
 * @param[in] actions Renames to perform.
 * @param[in] cwd Working directory of the case.
 * @param[in] config Configuration of the case.
 * @return The answer of the sandboxed process.
 */
ProtocolSetInformationFile::Rsp Rename(const std::vector<RenameAction>& actions, const std::filesystem::path& cwd,
                                       appbox::LauncherConfig& config)
{
    ProtocolSetInformationFile::Req req;
    for (const auto& action : actions)
    {
        ProtocolSetInformationFile::Req::Item item;
        item.action = "rename";
        item.source = appbox::WideToUTF8(action.source);
        item.target = appbox::WideToUTF8(action.target);
        item.rootDirectory = appbox::WideToUTF8(action.rootDirectory);
        item.access = "delete";
        item.replaceIfExists = action.replaceIfExists;
        req.items.push_back(item);
    }

    return ProbeSetInformationFile.Call(req, cwd, config).get<ProtocolSetInformationFile::Rsp>();
}

/**
 * @brief Pin the layer of the folder of the cases to `Write Copy`.
 *
 * The mode keeps every modification inside the sandbox, which is the rule
 * these cases are about: a rename of an entry the host does not hold must
 * never reach the host filesystem.
 *
 * @param[in] cwd Working directory of the case.
 * @return true on success.
 */
bool PinFolderToWriteCopy(const std::wstring& cwd)
{
    return WriteFsIsolationFile(
        cwd, {
                 { L"#USERPROFILE#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
                  appbox::FilesystemIsolation::WriteCopy }
    });
}

} // namespace

/**
 * Condition:
 * 1. The file exists in the lower layer of the resources only and the folder
 *    is isolated with `Write Copy`.
 * 2. The sandboxed process renames the file inside the folder.
 *
 * Expected:
 * 1. The rename succeeds, so the new name is an entry of the overlay and
 *    carries the content of the packed file.
 * 2. The old name is gone from the overlay and is hidden by a whiteout marker,
 *    so the view does not report the entry of the lower layer any more.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, Rename_LowerLayer)
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
    ASSERT_TRUE(PinFolderToWriteCopy(GetCWDString()));

    const auto rsp = Rename(
        {
            { Folder() + L"\\data.txt", Folder() + L"\\renamed.txt" }
    },
        GetCWD(), config);
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(1));
    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_SUCCESS));

    /* The new name is the file of the overlay and carries the packed content. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull(FolderInOverlay(GetCWDString()) + L"\\renamed.txt", data),
                  static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "packed");
    }

    /* The old name is gone from the overlay and is hidden by a whiteout marker. */
    EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\data.txt"));
    EXPECT_TRUE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\data.txt.$APPBOX_DELETE$"));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. The folder is isolated with `Merge` and the host holds the file
 *    `data.txt`.
 * 2. The sandboxed process renames the file inside the folder.
 *
 * Expected:
 * 1. The modification is applied to the host filesystem: the file carries the
 *    new name afterwards and the old name is gone.
 * 2. The overlay holds neither of the two names, so the rename never reached
 *    the sandbox.
 */
TEST_F(E2E_Fs, Rename_MergeWritesTheHostFile)
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
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
                       appbox::FilesystemIsolation::Merge }
    }));

    const auto rsp = Rename(
        {
            { Folder() + L"\\data.txt", Folder() + L"\\renamed.txt" }
    },
        GetCWD(), config);
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(1));
    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_SUCCESS));

    /* The host holds the new name and not the old one any more. */
    EXPECT_TRUE(host.FileExists(L"renamed.txt"));
    EXPECT_FALSE(host.FileExists(L"data.txt"));

    {
        std::string data;
        ASSERT_EQ(ReadFileFull((host.Get() / L"renamed.txt").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "host");
    }

    /* The overlay holds neither of the two names. */
    EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\data.txt"));
    EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\renamed.txt"));

    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. The folder is isolated with `Write Copy` and holds two files of the lower
 *    layer.
 * 2. The sandboxed process renames one file onto the name of the other one
 *    once without and once with the replace flag.
 *
 * Expected:
 * 1. The rename which does not replace the entry fails with
 *    `Name Collision`, because the view reports the name of the lower layer.
 * 2. The rename which replaces the entry succeeds: the overlay holds the new
 *    name with the content of the source and the source is hidden by a
 *    whiteout marker.
 * 3. The lower layer keeps both of its files.
 */
TEST_F(E2E_Fs, Rename_TargetExists)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"data.txt", "packed"),
                    FsFile(L"taken.txt", "taken")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(PinFolderToWriteCopy(GetCWDString()));

    const auto rsp = Rename(
        {
            { Folder() + L"\\data.txt", Folder() + L"\\taken.txt", L"", false },
            { Folder() + L"\\data.txt", Folder() + L"\\taken.txt", L"", true  },
    },
        GetCWD(), config);
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(2));
    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_OBJECT_NAME_COLLISION));
    EXPECT_EQ(rsp.items[1].status, static_cast<long>(STATUS_SUCCESS));

    /* The new name is the file of the overlay and carries the packed content. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull(FolderInOverlay(GetCWDString()) + L"\\taken.txt", data),
                  static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "packed");
    }

    EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\data.txt"));
    EXPECT_TRUE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\data.txt.$APPBOX_DELETE$"));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. The folder is isolated with `Full`, so the host folder is masked, and the
 *    host holds a file which carries the name the packed file is renamed to.
 * 2. The sandboxed process renames the packed file onto that name.
 *
 * Expected:
 * 1. The rename succeeds, because the name is free in the view: the isolation
 *    hides the entry of the host filesystem.
 * 2. The new entry is the file of the overlay and carries the packed content.
 * 3. The host file keeps its own content, so the rename never reached it.
 */
TEST_F(E2E_Fs, Rename_IsolationHidden)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"host.txt", "host"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"packed.txt", "packed")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
                       appbox::FilesystemIsolation::Full }
    }));

    const auto rsp = Rename(
        {
            { Folder() + L"\\packed.txt", Folder() + L"\\host.txt" }
    },
        GetCWD(), config);
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(1));
    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_SUCCESS));

    /* The new name is the file of the overlay and carries the packed content. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull(FolderInOverlay(GetCWDString()) + L"\\host.txt", data),
                  static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "packed");
    }

    /* The host file of the same name keeps its own content. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull((host.Get() / L"host.txt").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "host");
    }

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. The folder is isolated with `Write Copy` and holds two files of the lower
 *    layer.
 * 2. The sandboxed process renames one file with a name which is relative to
 *    the directory it names, and one with a name which is relative to the
 *    directory of the object itself.
 *
 * Expected:
 * 1. Both renames succeed, so a name which is no full path is resolved against
 *    the same base as the file system uses.
 * 2. The new names are entries of the overlay which carry the content of their
 *    source, and both sources are hidden by a whiteout marker.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, Rename_RelativeName)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"a.txt", "a"),
                    FsFile(L"c.txt", "c")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(PinFolderToWriteCopy(GetCWDString()));

    const auto rsp = Rename(
        {
            { Folder() + L"\\a.txt", L"b.txt", Folder(), false },
            { Folder() + L"\\c.txt", L"d.txt", L"",      false },
    },
        GetCWD(), config);
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(2));
    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[1].status, static_cast<long>(STATUS_SUCCESS));

    /* The new names are the files of the overlay and carry the content of
     * their source. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull(FolderInOverlay(GetCWDString()) + L"\\b.txt", data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "a");
        ASSERT_EQ(ReadFileFull(FolderInOverlay(GetCWDString()) + L"\\d.txt", data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "c");
    }

    /* Both sources are gone from the overlay and are hidden by a marker. */
    EXPECT_TRUE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\a.txt.$APPBOX_DELETE$"));
    EXPECT_TRUE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\c.txt.$APPBOX_DELETE$"));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. The folder is isolated with `Write Copy` and holds a file of the lower
 *    layer.
 * 2. The sandboxed process renames the file onto the name it already has, once
 *    without and once with the replace flag.
 *
 * Expected:
 * 1. Both renames succeed, which is what the file system reports for an object
 *    which is moved onto its own name.
 * 2. The entry stays in the view: the copy the open carried into the overlay
 *    keeps its content and no whiteout marker hides the name.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, Rename_SameName)
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
    ASSERT_TRUE(PinFolderToWriteCopy(GetCWDString()));

    const auto rsp = Rename(
        {
            { Folder() + L"\\data.txt", Folder() + L"\\data.txt", L"", false },
            { Folder() + L"\\data.txt", Folder() + L"\\data.txt", L"", true  },
    },
        GetCWD(), config);
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(2));
    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[1].status, static_cast<long>(STATUS_SUCCESS));

    /* The entry is still there, with its content, and nothing hides it. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull(FolderInOverlay(GetCWDString()) + L"\\data.txt", data),
                  static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "packed");
    }
    EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\data.txt.$APPBOX_DELETE$"));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. The overlay holds the folder `sub` with a file inside it, and the lower
 *    layer holds the folder `lower` with a file inside it; the layer of the
 *    cases is pinned to `Write Copy`.
 * 2. The sandboxed process renames the folder of the overlay and the folder of
 *    the lower layer.
 *
 * Expected:
 * 1. The folder of the overlay is renamed inside the overlay, together with
 *    the file it holds.
 * 2. The folder of the read-only layer is not renamed and nothing of that
 *    layer is carried into the overlay: the open of a directory cannot copy it
 *    up, so the second rename reports a failure and leaves both layers as they
 *    were.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, Rename_Directory)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {
            FsDir(L"filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true), {
                FsDir(kFolderName, {
                    FsDir(L"sub", {
                        FsFile(L"data.txt", "packed")
                    })
                })
            })
        }),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsDir(L"lower", {
                        FsFile(L"data.txt", "lower")
                    })
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(PinFolderToWriteCopy(GetCWDString()));

    const auto rsp = Rename(
        {
            { Folder() + L"\\sub",   Folder() + L"\\renamed", L"", false },
            { Folder() + L"\\lower", Folder() + L"\\moved",   L"", false },
    },
        GetCWD(), config);
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(2));
    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_NE(rsp.items[1].status, static_cast<long>(STATUS_SUCCESS));

    /* The folder of the overlay carries its new name and the file it holds. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull(FolderInOverlay(GetCWDString()) + L"\\renamed\\data.txt", data),
                  static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "packed");
    }
    EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\sub"));
    EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\moved"));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
