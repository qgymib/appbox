#include <gtest/gtest.h>
#include "filesystem/IsolationTable.hpp"
#include "filesystem/StreamName.hpp"
#include "src/core/FilesystemIsolationFile.hpp"
#include "src/core/FilesystemIsolationModel.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace
{

/** Path of an imported folder used by the cases below. */
constexpr wchar_t kAppFolder[] = L"#ProgramFiles#\\MyApp";

/** Path of a folder inside the imported folder. */
constexpr wchar_t kDataFolder[] = L"#ProgramFiles#\\MyApp\\data";

/** Path of a file inside the imported folder. */
constexpr wchar_t kAppFile[] = L"#ProgramFiles#\\MyApp\\app.exe";

/**
 * @brief Set a mode and fail the test when the model refuses it.
 * @param[in,out] model Model to update.
 * @param[in] path Path of the entry.
 * @param[in] kind Kind of the entry.
 * @param[in] isolation Mode to set.
 */
void SetMode(appbox::FilesystemIsolationModel& model, const std::wstring& path, appbox::FilesystemEntryKind kind,
             appbox::FilesystemIsolation isolation)
{
    std::string error;
    ASSERT_TRUE(model.SetIsolation(path, kind, isolation, error)) << error;
}

} // namespace

TEST(Unit_FilesystemIsolation, NamesAreOrderedLikeTheEnumeration)
{
    const auto& names = appbox::FilesystemIsolationNames();
    ASSERT_EQ(names.size(), 4u);
    EXPECT_EQ(names[0], L"Full");
    EXPECT_EQ(names[1], L"Write Copy");
    EXPECT_EQ(names[2], L"Merge");
    EXPECT_EQ(names[3], L"Whiteout");

    EXPECT_EQ(appbox::FilesystemIsolationName(appbox::FilesystemIsolation::Full), L"Full");
    EXPECT_EQ(appbox::FilesystemIsolationName(appbox::FilesystemIsolation::WriteCopy), L"Write Copy");
    EXPECT_EQ(appbox::FilesystemIsolationName(appbox::FilesystemIsolation::Merge), L"Merge");
    EXPECT_EQ(appbox::FilesystemIsolationName(appbox::FilesystemIsolation::Whiteout), L"Whiteout");
}

TEST(Unit_FilesystemIsolation, NamesOfAKindDropTheFolderOnlyModes)
{
    const auto& folders = appbox::FilesystemIsolationNamesFor(appbox::FilesystemEntryKind::Directory);
    ASSERT_EQ(folders.size(), 4u);
    EXPECT_EQ(folders[0], L"Full");
    EXPECT_EQ(folders[1], L"Write Copy");
    EXPECT_EQ(folders[2], L"Merge");
    EXPECT_EQ(folders[3], L"Whiteout");

    const auto& files = appbox::FilesystemIsolationNamesFor(appbox::FilesystemEntryKind::File);
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0], L"Full");
    EXPECT_EQ(files[1], L"Whiteout");
}

TEST(Unit_FilesystemIsolation, ParseNameIgnoresTheCase)
{
    appbox::FilesystemIsolation isolation = appbox::FilesystemIsolation::Whiteout;

    EXPECT_TRUE(appbox::ParseFilesystemIsolationName(L"full", isolation));
    EXPECT_EQ(isolation, appbox::FilesystemIsolation::Full);

    EXPECT_TRUE(appbox::ParseFilesystemIsolationName(L"WRITE COPY", isolation));
    EXPECT_EQ(isolation, appbox::FilesystemIsolation::WriteCopy);

    EXPECT_TRUE(appbox::ParseFilesystemIsolationName(L"merge", isolation));
    EXPECT_EQ(isolation, appbox::FilesystemIsolation::Merge);

    EXPECT_TRUE(appbox::ParseFilesystemIsolationName(L"whiteout", isolation));
    EXPECT_EQ(isolation, appbox::FilesystemIsolation::Whiteout);

    EXPECT_FALSE(appbox::ParseFilesystemIsolationName(L"hide", isolation));
    EXPECT_FALSE(appbox::ParseFilesystemIsolationName(L"", isolation));
}

TEST(Unit_FilesystemIsolation, TokensRoundTrip)
{
    for (const auto isolation : { appbox::FilesystemIsolation::Full, appbox::FilesystemIsolation::WriteCopy,
                                  appbox::FilesystemIsolation::Merge, appbox::FilesystemIsolation::Whiteout })
    {
        const std::string token = appbox::filesystem_isolation::IsolationToken(isolation);

        appbox::FilesystemIsolation parsed = appbox::FilesystemIsolation::Full;
        EXPECT_TRUE(appbox::filesystem_isolation::ParseIsolationToken(token, parsed));
        EXPECT_EQ(parsed, isolation);
    }

    EXPECT_STREQ(appbox::filesystem_isolation::IsolationToken(appbox::FilesystemIsolation::Merge), "merge");

    appbox::FilesystemIsolation parsed = appbox::FilesystemIsolation::Full;
    EXPECT_TRUE(appbox::filesystem_isolation::ParseIsolationToken("Write-Copy", parsed));
    EXPECT_EQ(parsed, appbox::FilesystemIsolation::WriteCopy);
    EXPECT_TRUE(appbox::filesystem_isolation::ParseIsolationToken("writecopy", parsed));
    EXPECT_EQ(parsed, appbox::FilesystemIsolation::WriteCopy);
    EXPECT_FALSE(appbox::filesystem_isolation::ParseIsolationToken("hide", parsed));
}

TEST(Unit_FilesystemIsolation, EntryKindTokensRoundTrip)
{
    EXPECT_STREQ(appbox::filesystem_isolation::EntryKindToken(appbox::FilesystemEntryKind::File), "file");
    EXPECT_STREQ(appbox::filesystem_isolation::EntryKindToken(appbox::FilesystemEntryKind::Directory), "directory");

    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::File;
    EXPECT_TRUE(appbox::filesystem_isolation::ParseEntryKindToken("Directory", kind));
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::Directory);
    EXPECT_TRUE(appbox::filesystem_isolation::ParseEntryKindToken("folder", kind));
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::Directory);
    EXPECT_TRUE(appbox::filesystem_isolation::ParseEntryKindToken("FILE", kind));
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::File);
    EXPECT_FALSE(appbox::filesystem_isolation::ParseEntryKindToken("link", kind));
}

TEST(Unit_FilesystemIsolation, OnlyAFolderAcceptsTheFolderOnlyModes)
{
    using appbox::FilesystemEntryKind;
    using appbox::FilesystemIsolation;

    EXPECT_TRUE(appbox::filesystem_isolation::IsAllowed(FilesystemIsolation::Full, FilesystemEntryKind::File));
    EXPECT_FALSE(appbox::filesystem_isolation::IsAllowed(FilesystemIsolation::WriteCopy, FilesystemEntryKind::File));
    EXPECT_FALSE(appbox::filesystem_isolation::IsAllowed(FilesystemIsolation::Merge, FilesystemEntryKind::File));
    EXPECT_TRUE(appbox::filesystem_isolation::IsAllowed(FilesystemIsolation::Whiteout, FilesystemEntryKind::File));

    for (const auto isolation : { FilesystemIsolation::Full, FilesystemIsolation::WriteCopy, FilesystemIsolation::Merge,
                                  FilesystemIsolation::Whiteout })
    {
        EXPECT_TRUE(appbox::filesystem_isolation::IsAllowed(isolation, FilesystemEntryKind::Directory));
    }
}

/**
 * @brief The default of the view is `Merge`, which a file cannot hold.
 *
 * A folder the user never touched follows the default of the view; a file
 * cannot hold `Merge`, so the workspace shows `Full` for a file which no entry
 * covers while the sandbox takes the mode of the folder that holds it.
 */
TEST(Unit_FilesystemIsolation, DefaultsAreMergeForAFolderAndFullForAFile)
{
    EXPECT_EQ(appbox::filesystem_isolation::kDefaultIsolation, appbox::FilesystemIsolation::Merge);
    EXPECT_EQ(appbox::DefaultFilesystemIsolation(appbox::FilesystemEntryKind::Directory),
              appbox::filesystem_isolation::kDefaultIsolation);
    EXPECT_EQ(appbox::DefaultFilesystemIsolation(appbox::FilesystemEntryKind::File), appbox::FilesystemIsolation::Full);
}

TEST(Unit_FilesystemIsolation, WriteCopyIsExpressedAsFullForAFile)
{
    using appbox::FilesystemEntryKind;
    using appbox::FilesystemIsolation;

    EXPECT_EQ(appbox::FilesystemIsolationForKind(FilesystemIsolation::WriteCopy, FilesystemEntryKind::File),
              FilesystemIsolation::Full);
    EXPECT_EQ(appbox::FilesystemIsolationForKind(FilesystemIsolation::Whiteout, FilesystemEntryKind::File),
              FilesystemIsolation::Whiteout);
    EXPECT_EQ(appbox::FilesystemIsolationForKind(FilesystemIsolation::WriteCopy, FilesystemEntryKind::Directory),
              FilesystemIsolation::WriteCopy);

    /*
     * `Merge` stays `Merge` for a file as well: a file below a `Merge` folder
     * is written to the host filesystem whenever the host holds it, which
     * `Full` would not tell the user.
     */
    EXPECT_EQ(appbox::FilesystemIsolationForKind(FilesystemIsolation::Merge, FilesystemEntryKind::File),
              FilesystemIsolation::Merge);
    EXPECT_EQ(appbox::FilesystemIsolationForKind(FilesystemIsolation::Merge, FilesystemEntryKind::Directory),
              FilesystemIsolation::Merge);
}

TEST(Unit_FilesystemIsolation, PathHelpersNormalizeAndSplit)
{
    const auto parts = appbox::SplitViewPath(L"#ProgramFiles#/MyApp\\data");
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0], L"#ProgramFiles#");
    EXPECT_EQ(parts[1], L"MyApp");
    EXPECT_EQ(parts[2], L"data");

    EXPECT_EQ(appbox::JoinViewPath(L"#ProgramFiles#", L"MyApp"), kAppFolder);
    EXPECT_EQ(appbox::JoinViewPath(L"", L"MyApp"), L"MyApp");
    EXPECT_EQ(appbox::JoinViewPath(L"#ProgramFiles#", L""), L"#ProgramFiles#");

    EXPECT_EQ(appbox::ViewPathParent(kDataFolder), kAppFolder);
    EXPECT_EQ(appbox::ViewPathParent(L"#ProgramFiles#"), L"");
    EXPECT_EQ(appbox::ViewPathLeafName(kAppFile), L"app.exe");
    EXPECT_EQ(appbox::ViewPathLeafName(L"#ProgramFiles#"), L"#ProgramFiles#");

    EXPECT_EQ(appbox::NormalizeViewPath(L"#ProgramFiles#\\MyApp\\.\\data"), kDataFolder);
    EXPECT_EQ(appbox::NormalizeViewPath(L"/#ProgramFiles#/MyApp/"), kAppFolder);

    /* A parent reference would leave the virtual filesystem. */
    EXPECT_TRUE(appbox::NormalizeViewPath(L"#ProgramFiles#\\..\\Windows").empty());
}

TEST(Unit_FilesystemIsolation, PathComparisonIgnoresTheCase)
{
    EXPECT_TRUE(appbox::ViewPathEquals(kAppFolder, L"#programfiles#\\myapp"));
    EXPECT_TRUE(appbox::ViewPathEquals(kDataFolder, L"#ProgramFiles#\\MyApp\\data\\"));
    EXPECT_FALSE(appbox::ViewPathEquals(kDataFolder, kAppFolder));

    EXPECT_TRUE(appbox::IsViewPathBelow(kDataFolder, kAppFolder));
    EXPECT_FALSE(appbox::IsViewPathBelow(kAppFolder, kDataFolder));

    /* The ancestor has to be a whole component prefix. */
    EXPECT_FALSE(appbox::IsViewPathBelow(L"#ProgramFiles#\\MyAppData", kAppFolder));
    EXPECT_FALSE(appbox::IsViewPathBelow(kAppFolder, kAppFolder));
    EXPECT_FALSE(appbox::IsViewPathBelow(kAppFolder, L""));
}

TEST(Unit_FilesystemIsolation, FreshModelFollowsTheDefaults)
{
    appbox::FilesystemIsolationModel model;

    EXPECT_TRUE(model.IsEmpty());
    EXPECT_FALSE(model.HasExplicitIsolation(kAppFolder));
    EXPECT_EQ(model.EffectiveIsolation(kAppFolder, appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Merge);
    EXPECT_EQ(model.EffectiveIsolation(kAppFile, appbox::FilesystemEntryKind::File), appbox::FilesystemIsolation::Full);
}

TEST(Unit_FilesystemIsolation, SetIsolationStoresOneEntry)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, L"#ProgramFiles#\\MyApp\\.\\data", appbox::FilesystemEntryKind::Directory,
            appbox::FilesystemIsolation::Whiteout);

    EXPECT_FALSE(model.IsEmpty());
    EXPECT_TRUE(model.HasExplicitIsolation(kDataFolder));
    EXPECT_TRUE(model.HasExplicitIsolation(L"#programfiles#\\myapp\\DATA"));
    EXPECT_EQ(model.EffectiveIsolation(kDataFolder, appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Whiteout);

    ASSERT_EQ(model.Entries().size(), 1u);
    EXPECT_EQ(model.Entries()[0].path, kDataFolder);
    EXPECT_EQ(model.Entries()[0].kind, appbox::FilesystemEntryKind::Directory);
    EXPECT_EQ(model.Entries()[0].isolation, appbox::FilesystemIsolation::Whiteout);
}

TEST(Unit_FilesystemIsolation, SetIsolationUpdatesAnExistingEntry)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Whiteout);
    SetMode(model, L"#PROGRAMFILES#\\MYAPP", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);

    ASSERT_EQ(model.Entries().size(), 1u);
    EXPECT_EQ(model.Entries()[0].isolation, appbox::FilesystemIsolation::Full);
    /* The spelling the user picked first is kept. */
    EXPECT_EQ(model.Entries()[0].path, kAppFolder);
}

TEST(Unit_FilesystemIsolation, SetIsolationRefusesInvalidInput)
{
    appbox::FilesystemIsolationModel model;
    std::string                      error;

    /* A path which leaves the view names no entry. */
    EXPECT_FALSE(model.SetIsolation(L"#ProgramFiles#\\..\\Windows", appbox::FilesystemEntryKind::Directory,
                                    appbox::FilesystemIsolation::Full, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(
        model.SetIsolation(kAppFile, appbox::FilesystemEntryKind::File, appbox::FilesystemIsolation::WriteCopy, error));
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("write_copy"), std::string::npos);

    /* `Merge` is a mode of a folder, so a file refuses it as well. */
    error.clear();
    EXPECT_FALSE(
        model.SetIsolation(kAppFile, appbox::FilesystemEntryKind::File, appbox::FilesystemIsolation::Merge, error));
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("merge"), std::string::npos);

    /* A refused call never changes the model. */
    EXPECT_TRUE(model.IsEmpty());
}

/**
 * @brief The root of the view decides the paths no other entry covers.
 *
 * The root is the entry which carries an empty path. It is the folder a
 * location outside the recorded paths belongs to, which includes the locations
 * which are not part of the virtual filesystem at all, so its mode is the mode
 * of every path no listed folder names.
 */
TEST(Unit_FilesystemIsolation, TheRootOfTheViewIsAnEntryOfItsOwn)
{
    appbox::FilesystemIsolationModel model;
    std::string                      error;

    /* Without a root entry a path no entry covers follows the default of the view. */
    EXPECT_FALSE(model.HasExplicitIsolation(L""));
    EXPECT_EQ(model.EffectiveIsolation(L"#Windows#\\System32", appbox::FilesystemEntryKind::Directory),
              appbox::filesystem_isolation::kDefaultIsolation);

    ASSERT_TRUE(
        model.SetIsolation(L"", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full, error))
        << error;

    EXPECT_TRUE(model.HasExplicitIsolation(L""));
    ASSERT_EQ(model.Entries().size(), 1u);
    EXPECT_EQ(model.Entries()[0].path, L"");
    EXPECT_EQ(model.Entries()[0].kind, appbox::FilesystemEntryKind::Directory);
    EXPECT_EQ(model.Entries()[0].isolation, appbox::FilesystemIsolation::Full);

    /* The root covers the paths of the view and the paths outside of it. */
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#\\MyApp\\data", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Full);
    EXPECT_EQ(model.EffectiveIsolation(L"#Windows#\\System32\\kernel32.dll", appbox::FilesystemEntryKind::File),
              appbox::FilesystemIsolation::Full);

    /* A folder below the root overrides it. */
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Merge);
    EXPECT_EQ(model.EffectiveIsolation(kDataFolder, appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Merge);
    EXPECT_EQ(model.EffectiveIsolation(L"#Windows#\\System32", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Full);

    /* The root is updated in place like every other entry. */
    ASSERT_TRUE(
        model.SetIsolation(L"", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Whiteout, error))
        << error;
    ASSERT_EQ(model.Entries().size(), 2u);
    EXPECT_EQ(model.Entries()[0].isolation, appbox::FilesystemIsolation::Whiteout);
}

/**
 * @brief The recursion of the isolation dialog overwrites the folders below.
 *
 * The dialog applies a mode to the folder the user picked and, while the
 * recursion was chosen, to the folders below it. The files below the folder
 * keep their own modes: a file cannot hold every folder mode, and a file which
 * carries none of its own follows the folder above it anyway.
 */
TEST(Unit_FilesystemIsolation, ApplyIsolationToSubtreeOverwritesTheFoldersBelow)
{
    appbox::FilesystemIsolationModel model;
    std::string                      error;

    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Whiteout);
    SetMode(model, kDataFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    SetMode(model, L"#ProgramFiles#\\MyApp\\data\\logs", appbox::FilesystemEntryKind::Directory,
            appbox::FilesystemIsolation::Full);
    SetMode(model, L"#ProgramFiles#\\MyApp\\data\\app.ini", appbox::FilesystemEntryKind::File,
            appbox::FilesystemIsolation::Whiteout);
    SetMode(model, L"#ProgramFiles#\\Other", appbox::FilesystemEntryKind::Directory,
            appbox::FilesystemIsolation::Whiteout);

    ASSERT_TRUE(model.ApplyIsolationToSubtree(kAppFolder, appbox::FilesystemIsolation::Merge, error)) << error;

    EXPECT_EQ(model.EffectiveIsolation(kAppFolder, appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Merge);
    EXPECT_EQ(model.EffectiveIsolation(kDataFolder, appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Merge);
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#\\MyApp\\data\\logs", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Merge);

    /* A folder outside the subtree and a file inside it keep their own modes. */
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#\\Other", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Whiteout);
    EXPECT_TRUE(model.HasExplicitIsolation(L"#ProgramFiles#\\MyApp\\data\\app.ini"));
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#\\MyApp\\data\\app.ini", appbox::FilesystemEntryKind::File),
              appbox::FilesystemIsolation::Whiteout);

    /* The recursion creates the folder it was asked for. */
    ASSERT_TRUE(model.ApplyIsolationToSubtree(L"#ProgramFiles#\\New", appbox::FilesystemIsolation::Full, error))
        << error;
    EXPECT_TRUE(model.HasExplicitIsolation(L"#ProgramFiles#\\New"));
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#\\New\\sub", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Full);

    /* The root reaches every folder the model holds. */
    ASSERT_TRUE(model.ApplyIsolationToSubtree(L"", appbox::FilesystemIsolation::Merge, error)) << error;
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#\\Other", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Merge);
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#\\New", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Merge);

    /* A path which leaves the view is refused without changing the model. */
    const auto count = model.Entries().size();
    EXPECT_FALSE(
        model.ApplyIsolationToSubtree(L"#ProgramFiles#\\..\\Windows", appbox::FilesystemIsolation::Full, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(model.Entries().size(), count);
}

/**
 * @brief The mode of the container reaches the layers of the view only when
 *        the recursion of the dialog was chosen.
 *
 * The preset directories are the roots of the layers of the view and not
 * folders below the container, so the mode picked for the container has to
 * keep them: the call pins every layer root which carries no mode of its own
 * to the mode which applies to it today.
 */
TEST(Unit_FilesystemIsolation, SetRootIsolationKeepsTheLayersOfTheView)
{
    appbox::FilesystemIsolationModel model;
    std::string                      error;

    const std::vector<std::wstring> layers = { L"#ProgramFiles#", L"#Windows#", L"#USERPROFILE#" };
    ASSERT_TRUE(model.SetRootIsolation(appbox::FilesystemIsolation::Whiteout, layers, error)) << error;

    /* The root of the view and one entry per layer are listed. */
    ASSERT_EQ(model.Entries().size(), 4u);
    EXPECT_EQ(model.Entries()[0].path, L"");
    EXPECT_EQ(model.Entries()[0].kind, appbox::FilesystemEntryKind::Directory);
    EXPECT_EQ(model.Entries()[0].isolation, appbox::FilesystemIsolation::Whiteout);

    /* Every layer keeps the mode it showed before the container changed. */
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#", appbox::FilesystemEntryKind::Directory),
              appbox::filesystem_isolation::kDefaultIsolation);
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#\\MyApp\\data", appbox::FilesystemEntryKind::Directory),
              appbox::filesystem_isolation::kDefaultIsolation);
    EXPECT_EQ(model.EffectiveIsolation(L"#Windows#\\System32", appbox::FilesystemEntryKind::Directory),
              appbox::filesystem_isolation::kDefaultIsolation);

    /* A location outside the layers follows the mode of the container. */
    EXPECT_EQ(model.EffectiveIsolation(L"#Other#", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Whiteout);
    EXPECT_EQ(model.EffectiveIsolation(L"", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Whiteout);
}

/**
 * @brief A layer which carries a mode of its own keeps it.
 *
 * The mode of the container reaches the layers which follow it, so only a
 * layer without an entry of its own is pinned; an entry the user set before
 * stays what it is.
 */
TEST(Unit_FilesystemIsolation, SetRootIsolationKeepsAnExplicitLayerMode)
{
    appbox::FilesystemIsolationModel model;
    std::string                      error;

    SetMode(model, L"#ProgramFiles#", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    SetMode(model, L"#ProgramFiles#\\MyApp", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);

    ASSERT_TRUE(
        model.SetRootIsolation(appbox::FilesystemIsolation::Whiteout, { L"#ProgramFiles#", L"#Windows#" }, error))
        << error;

    /* The layer and the entries below it keep their own modes. */
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Full);
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#\\MyApp\\data", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Full);

    /* The layer without a mode of its own is pinned to what it showed. */
    EXPECT_TRUE(model.HasExplicitIsolation(L"#Windows#"));
    EXPECT_EQ(model.EffectiveIsolation(L"#Windows#\\System32", appbox::FilesystemEntryKind::Directory),
              appbox::filesystem_isolation::kDefaultIsolation);
    EXPECT_EQ(model.EffectiveIsolation(L"", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Whiteout);
}

/**
 * @brief A container which already carries the mode is left alone.
 *
 * The mode of the view is the mode of an untouched container, so picking it
 * writes no entry at all; the same holds for a container which was given the
 * mode before.
 */
TEST(Unit_FilesystemIsolation, SetRootIsolationWithoutAChangeWritesNoEntry)
{
    appbox::FilesystemIsolationModel model;
    std::string                      error;

    const std::vector<std::wstring> layers = { L"#ProgramFiles#", L"#Windows#" };

    ASSERT_TRUE(model.SetRootIsolation(appbox::filesystem_isolation::kDefaultIsolation, layers, error)) << error;
    EXPECT_TRUE(model.IsEmpty());

    ASSERT_TRUE(model.SetRootIsolation(appbox::FilesystemIsolation::Whiteout, layers, error)) << error;
    const auto count = model.Entries().size();
    ASSERT_TRUE(model.SetRootIsolation(appbox::FilesystemIsolation::Whiteout, layers, error)) << error;
    EXPECT_EQ(model.Entries().size(), count);
}

/**
 * @brief A layer root which does not name a folder is refused.
 *
 * The call is the one of the dialog, so a layer of the view which cannot be
 * addressed has to leave the model unchanged instead of pinning half of it.
 */
TEST(Unit_FilesystemIsolation, SetRootIsolationRefusesAnInvalidLayer)
{
    appbox::FilesystemIsolationModel model;
    std::string                      error;

    EXPECT_FALSE(
        model.SetRootIsolation(appbox::FilesystemIsolation::Whiteout, { L"#ProgramFiles#\\..\\Windows" }, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.SetRootIsolation(appbox::FilesystemIsolation::Whiteout, { L"" }, error));
    EXPECT_FALSE(error.empty());

    /* A refused call never changes the model. */
    EXPECT_TRUE(model.IsEmpty());
}

/**
 * @brief The recursion of the dialog overwrites the pinned layers as well.
 */
TEST(Unit_FilesystemIsolation, SetRootIsolationIsOverwrittenByTheRecursion)
{
    appbox::FilesystemIsolationModel model;
    std::string                      error;

    ASSERT_TRUE(
        model.SetRootIsolation(appbox::FilesystemIsolation::Whiteout, { L"#ProgramFiles#", L"#Windows#" }, error))
        << error;
    ASSERT_TRUE(model.ApplyIsolationToSubtree(L"", appbox::FilesystemIsolation::Full, error)) << error;

    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Full);
    EXPECT_EQ(model.EffectiveIsolation(L"#Windows#\\System32", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Full);
}

/**
 * @brief The layers of a container mode travel to the sandbox.
 *
 * The modes the packer writes are read back by the table of the sandbox, so a
 * path below a layer root has to land on the entry of the layer while a
 * location no layer holds lands on the entry of the root.
 */
TEST(Unit_FilesystemIsolation, ThePinnedLayersTravelThroughTheIsolationFile)
{
    appbox::FilesystemIsolationModel model;
    std::string                      error;
    ASSERT_TRUE(model.SetRootIsolation(appbox::FilesystemIsolation::Whiteout, { L"#ProgramFiles#" }, error)) << error;

    std::string text;
    ASSERT_TRUE(appbox::BuildFilesystemIsolationFile(model, text, error)) << error;

    const std::vector<appbox::filesystem::IsolationLayer> layers = {
        { L"#ProgramFiles#", L"\\??\\C:\\Program Files" },
    };

    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>          unmapped;
    ASSERT_TRUE(table.Parse(text, layers, unmapped, error)) << error;
    EXPECT_TRUE(unmapped.empty());
    EXPECT_EQ(table.Count(), 2u);

    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::Full;
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::File;

    /* The layer keeps the mode it showed, so the container does not reach it. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\data", mode, kind));
    EXPECT_EQ(mode, appbox::filesystem_isolation::kDefaultIsolation);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::Directory);

    /* A location outside the layers follows the mode of the container. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Windows\\System32", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Whiteout);
}

TEST(Unit_FilesystemIsolation, AChildOverridesTheFolderAbove)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Whiteout);

    /* Without a mode of its own the child follows the folder above it. */
    EXPECT_EQ(model.EffectiveIsolation(kDataFolder, appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Whiteout);

    SetMode(model, kDataFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    EXPECT_EQ(model.EffectiveIsolation(kDataFolder, appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Full);

    /* The folder above keeps its own mode, the entries below the child follow the child. */
    EXPECT_EQ(model.EffectiveIsolation(kAppFolder, appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Whiteout);
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#\\MyApp\\data\\logs", appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Full);
}

TEST(Unit_FilesystemIsolation, AWhiteoutFolderHidesTheEntriesBelowIt)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Whiteout);

    EXPECT_EQ(model.EffectiveIsolation(kAppFile, appbox::FilesystemEntryKind::File),
              appbox::FilesystemIsolation::Whiteout);
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#\\MyApp\\data\\settings.ini", appbox::FilesystemEntryKind::File),
              appbox::FilesystemIsolation::Whiteout);
}

TEST(Unit_FilesystemIsolation, AMergedFolderShowsFullForAFile)
{
    appbox::FilesystemIsolationModel model;

    /* A file which no entry covers follows `Full`, which is what the workspace
     * shows for a file, because a file cannot hold `Merge`. */
    EXPECT_EQ(model.EffectiveIsolation(kAppFile, appbox::FilesystemEntryKind::File), appbox::FilesystemIsolation::Full);

    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    EXPECT_EQ(model.EffectiveIsolation(kAppFile, appbox::FilesystemEntryKind::File), appbox::FilesystemIsolation::Full);

    /* A file keeps its own mode even when a folder above it is hidden. */
    SetMode(model, kAppFile, appbox::FilesystemEntryKind::File, appbox::FilesystemIsolation::Whiteout);
    EXPECT_EQ(model.EffectiveIsolation(kAppFile, appbox::FilesystemEntryKind::File),
              appbox::FilesystemIsolation::Whiteout);

    /*
     * A file below a `Merge` folder reports the mode of the folder: the mode
     * tells the user that a write of the file reaches the host filesystem,
     * which the fold to `Full` would hide. The file above keeps the mode it
     * was given.
     */
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Merge);
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#\\MyApp\\other.exe", appbox::FilesystemEntryKind::File),
              appbox::FilesystemIsolation::Merge);
    EXPECT_EQ(model.EffectiveIsolation(kAppFile, appbox::FilesystemEntryKind::File),
              appbox::FilesystemIsolation::Whiteout);
}

TEST(Unit_FilesystemIsolation, RemoveSubtreeDropsTheEntryAndItsDescendants)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    SetMode(model, kDataFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Whiteout);
    SetMode(model, L"#ProgramFiles#\\MyApp\\data\\logs", appbox::FilesystemEntryKind::Directory,
            appbox::FilesystemIsolation::Full);
    SetMode(model, L"#ProgramFiles#\\MyApp\\data2", appbox::FilesystemEntryKind::Directory,
            appbox::FilesystemIsolation::Full);

    /* The comparison ignores the case, like the paths of the view do. */
    EXPECT_TRUE(model.RemoveSubtree(L"#programfiles#\\myapp\\DATA"));

    EXPECT_TRUE(model.HasExplicitIsolation(kAppFolder));
    EXPECT_TRUE(model.HasExplicitIsolation(L"#ProgramFiles#\\MyApp\\data2"));
    EXPECT_FALSE(model.HasExplicitIsolation(kDataFolder));
    EXPECT_FALSE(model.HasExplicitIsolation(L"#ProgramFiles#\\MyApp\\data\\logs"));

    /* A path the model does not hold removes nothing. */
    EXPECT_FALSE(model.RemoveSubtree(L"#Windows#"));
    EXPECT_FALSE(model.RemoveSubtree(L""));
}

TEST(Unit_FilesystemIsolation, EntriesAreOrderedByPath)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, L"#Windows#", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    SetMode(model, L"#appdata#", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    SetMode(model, L"#Windows#\\System32", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);

    /* The root of the view comes first, because its path is the shortest one. */
    SetMode(model, L"", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Merge);

    const auto& entries = model.Entries();
    ASSERT_EQ(entries.size(), 4u);
    EXPECT_EQ(entries[0].path, L"");
    EXPECT_EQ(entries[1].path, L"#appdata#");
    EXPECT_EQ(entries[2].path, L"#Windows#");
    EXPECT_EQ(entries[3].path, L"#Windows#\\System32");
}

TEST(Unit_FilesystemIsolation, AddEntryRefusesDuplicatesAndInvalidModes)
{
    appbox::FilesystemIsolationModel model;
    std::string                      error;

    appbox::FilesystemIsolationEntry entry;
    entry.path = L"#ProgramFiles#\\MyApp\\.\\data";
    entry.kind = appbox::FilesystemEntryKind::Directory;
    entry.isolation = appbox::FilesystemIsolation::Whiteout;
    ASSERT_TRUE(model.AddEntry(entry, error)) << error;
    EXPECT_EQ(model.Entries()[0].path, kDataFolder);

    error.clear();
    EXPECT_FALSE(model.AddEntry(entry, error));
    EXPECT_FALSE(error.empty());

    appbox::FilesystemIsolationEntry file_entry;
    file_entry.path = kAppFile;
    file_entry.kind = appbox::FilesystemEntryKind::File;
    file_entry.isolation = appbox::FilesystemIsolation::WriteCopy;

    error.clear();
    EXPECT_FALSE(model.AddEntry(file_entry, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    appbox::FilesystemIsolationEntry empty_entry;
    empty_entry.path = L"#ProgramFiles#\\..";
    EXPECT_FALSE(model.AddEntry(empty_entry, error));
    EXPECT_FALSE(error.empty());

    /*
     * An entry without a path is the root of the view, which is a folder: the
     * document of a session may list it, a file may not.
     */
    error.clear();
    appbox::FilesystemIsolationEntry root_entry;
    root_entry.path = L"";
    root_entry.kind = appbox::FilesystemEntryKind::Directory;
    root_entry.isolation = appbox::FilesystemIsolation::Merge;
    ASSERT_TRUE(model.AddEntry(root_entry, error)) << error;
    EXPECT_TRUE(model.HasExplicitIsolation(L""));

    error.clear();
    root_entry.isolation = appbox::FilesystemIsolation::Full;
    EXPECT_FALSE(model.AddEntry(root_entry, error));
    EXPECT_FALSE(error.empty());
}

TEST(Unit_FilesystemIsolation, ResetDropsEveryMode)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Whiteout);

    model.Reset();

    EXPECT_TRUE(model.IsEmpty());
    EXPECT_FALSE(model.HasExplicitIsolation(kAppFolder));
    EXPECT_EQ(model.EffectiveIsolation(kAppFolder, appbox::FilesystemEntryKind::Directory),
              appbox::filesystem_isolation::kDefaultIsolation);
}

TEST(Unit_FilesystemIsolation, BuildIsolationFileListsTheExplicitEntries)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    SetMode(model, kAppFile, appbox::FilesystemEntryKind::File, appbox::FilesystemIsolation::Whiteout);

    std::string text;
    std::string error;
    ASSERT_TRUE(appbox::BuildFilesystemIsolationFile(model, text, error)) << error;

    const auto document = nlohmann::json::parse(text);
    EXPECT_EQ(document[appbox::filesystem_isolation::kVersionKey].get<int>(), appbox::filesystem_isolation::kVersion);

    const auto& entries = document[appbox::filesystem_isolation::kEntriesKey];
    ASSERT_EQ(entries.size(), 2u);

    EXPECT_EQ(entries[0][appbox::filesystem_isolation::kPathKey].get<std::string>(), "#ProgramFiles#\\MyApp");
    EXPECT_EQ(entries[0][appbox::filesystem_isolation::kKindKey].get<std::string>(), "directory");
    EXPECT_EQ(entries[0][appbox::filesystem_isolation::kIsolationKey].get<std::string>(), "full");

    EXPECT_EQ(entries[1][appbox::filesystem_isolation::kPathKey].get<std::string>(), "#ProgramFiles#\\MyApp\\app.exe");
    EXPECT_EQ(entries[1][appbox::filesystem_isolation::kKindKey].get<std::string>(), "file");
    EXPECT_EQ(entries[1][appbox::filesystem_isolation::kIsolationKey].get<std::string>(), "whiteout");
}

TEST(Unit_FilesystemIsolation, BuildIsolationFileOfAModelWithoutModes)
{
    appbox::FilesystemIsolationModel model;

    std::string text;
    std::string error;
    ASSERT_TRUE(appbox::BuildFilesystemIsolationFile(model, text, error)) << error;

    const auto document = nlohmann::json::parse(text);
    EXPECT_EQ(document[appbox::filesystem_isolation::kVersionKey].get<int>(), appbox::filesystem_isolation::kVersion);
    ASSERT_TRUE(document[appbox::filesystem_isolation::kEntriesKey].is_array());
    EXPECT_TRUE(document[appbox::filesystem_isolation::kEntriesKey].empty());
}

/**
 * @brief The file the packer writes is the file the sandbox reads.
 *
 * The two sides of the isolation live in different components, so the test
 * pins that the document of the packer is accepted by the table of the sandbox
 * and resolves to the modes of the workspace.
 */
TEST(Unit_FilesystemIsolation, TheIsolationFileOfThePackerIsReadByTheSandbox)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    SetMode(model, kAppFile, appbox::FilesystemEntryKind::File, appbox::FilesystemIsolation::Whiteout);

    std::string text;
    std::string error;
    ASSERT_TRUE(appbox::BuildFilesystemIsolationFile(model, text, error)) << error;

    const std::vector<appbox::filesystem::IsolationLayer> layers = {
        { L"#ProgramFiles#", L"\\??\\C:\\Program Files" },
    };

    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>          unmapped;
    ASSERT_TRUE(table.Parse(text, layers, unmapped, error)) << error;
    EXPECT_TRUE(unmapped.empty());
    EXPECT_EQ(table.Count(), 2u);

    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::WriteCopy;
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::Directory;

    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Full);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::Directory);

    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\app.exe", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Whiteout);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::File);

    /* The entries below the folder follow it. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\data\\settings.ini", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Full);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::Directory);
}

/**
 * @brief The root entry of the isolation file covers the whole view.
 *
 * The root is the entry without a path: the packer writes it for the container
 * of the filesystem tree, and the sandbox applies it to every path no other
 * entry covers, including the locations outside the layers of the run.
 */
TEST(Unit_FilesystemIsolation, TheRootEntryTravelsThroughTheIsolationFile)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, L"", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Merge);
    SetMode(model, kAppFile, appbox::FilesystemEntryKind::File, appbox::FilesystemIsolation::Whiteout);

    std::string text;
    std::string error;
    ASSERT_TRUE(appbox::BuildFilesystemIsolationFile(model, text, error)) << error;

    const auto  document = nlohmann::json::parse(text);
    const auto& entries = document[appbox::filesystem_isolation::kEntriesKey];
    ASSERT_EQ(entries.size(), 2u);

    /* The root of the view is written as an entry without a path. */
    EXPECT_EQ(entries[0][appbox::filesystem_isolation::kPathKey].get<std::string>(), "");
    EXPECT_EQ(entries[0][appbox::filesystem_isolation::kKindKey].get<std::string>(), "directory");
    EXPECT_EQ(entries[0][appbox::filesystem_isolation::kIsolationKey].get<std::string>(), "merge");

    const std::vector<appbox::filesystem::IsolationLayer> layers = {
        { L"#ProgramFiles#", L"\\??\\C:\\Program Files" },
    };

    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>          unmapped;
    ASSERT_TRUE(table.Parse(text, layers, unmapped, error)) << error;
    EXPECT_TRUE(unmapped.empty());
    EXPECT_EQ(table.Count(), 2u);

    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::Full;
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::File;

    /* The root covers a path of a layer, a path outside every layer and the entries. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\data", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Merge);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::Directory);

    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Windows\\System32\\kernel32.dll", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Merge);

    /* An entry of the document still wins over the root. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\app.exe", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Whiteout);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::File);
}

/**
 * @brief Every mode of the table is explained by a description.
 *
 * The description is the text the workspace shows for the mode of a row and for
 * the modes the `Isolation` column offers, so every mode has to name itself and
 * its own rule.
 */
TEST(Unit_FilesystemIsolation, EveryModeIsDescribedForATooltip)
{
    for (const auto isolation : { appbox::FilesystemIsolation::Full, appbox::FilesystemIsolation::WriteCopy,
                                  appbox::FilesystemIsolation::Merge, appbox::FilesystemIsolation::Whiteout })
    {
        const std::wstring description =
            appbox::FilesystemIsolationDescription(isolation, appbox::FilesystemEntryKind::Directory);

        EXPECT_NE(description.find(L"Isolation mode"), std::wstring::npos) << description;
        EXPECT_NE(description.find(appbox::FilesystemIsolationName(isolation)), std::wstring::npos) << description;
    }

    EXPECT_NE(appbox::FilesystemIsolationDescription(appbox::FilesystemIsolation::Whiteout,
                                                     appbox::FilesystemEntryKind::Directory)
                  .find(L"invisible"),
              std::wstring::npos);
    EXPECT_NE(appbox::FilesystemIsolationDescription(appbox::FilesystemIsolation::Merge,
                                                     appbox::FilesystemEntryKind::Directory)
                  .find(L"default mode of a folder"),
              std::wstring::npos);
    EXPECT_NE(appbox::FilesystemIsolationDescription(appbox::FilesystemIsolation::Merge,
                                                     appbox::FilesystemEntryKind::Directory)
                  .find(L"host filesystem"),
              std::wstring::npos);
}

/**
 * @brief `Full` is described for the kind it was asked for.
 *
 * A folder is hidden from the application together with everything below it,
 * while a file keeps its host copy readable and only redirects the writes, so
 * the description of the two differs.
 */
TEST(Unit_FilesystemIsolation, FullTellsAFolderAndAFileApart)
{
    const auto folder = appbox::FilesystemIsolationDescription(appbox::FilesystemIsolation::Full,
                                                               appbox::FilesystemEntryKind::Directory);
    const auto file =
        appbox::FilesystemIsolationDescription(appbox::FilesystemIsolation::Full, appbox::FilesystemEntryKind::File);

    EXPECT_NE(folder.find(L"hidden"), std::wstring::npos) << folder;
    EXPECT_EQ(folder.find(L"host file stays readable"), std::wstring::npos) << folder;
    EXPECT_NE(file.find(L"host file stays readable"), std::wstring::npos) << file;
    EXPECT_NE(folder, file);
}

/**
 * @brief Build the text of an isolation file which lists one entry.
 * @param[in] path Path of the entry in the virtual filesystem.
 * @param[in] isolation Mode of the entry.
 * @param[in] kind Kind of the entry.
 * @return The UTF-8 text of the document.
 */
static std::string IsolationFileOf(const std::string& path, appbox::FilesystemIsolation isolation,
                                   appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::Directory)
{
    appbox::filesystem_isolation::Entry entry;
    entry.path = path;
    entry.kind = kind;
    entry.isolation = isolation;

    appbox::filesystem_isolation::Document document;
    document.entries.push_back(std::move(entry));
    return nlohmann::json(document).dump(2);
}

/**
 * @brief The isolation files of the layers of a run are applied in layer order.
 *
 * A patch package overrides the resources it carries and not the resources of
 * the layers below it, so the mode of a path a later file names is the mode of
 * that file while a path it does not name keeps the mode of the file below it.
 */
TEST(Unit_FilesystemIsolation, TheFileOfALaterLayerOverridesThePathItNames)
{
    const std::vector<appbox::filesystem::IsolationLayer> layers = {
        { L"#ProgramFiles#", L"\\??\\C:\\Program Files" },
    };

    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>          unmapped;
    std::string                        error;

    /* The resources of the archive. */
    ASSERT_TRUE(table.Parse(IsolationFileOf("#ProgramFiles#\\MyApp", appbox::FilesystemIsolation::Full), layers,
                            unmapped, error))
        << error;
    ASSERT_TRUE(table.Parse(IsolationFileOf("#ProgramFiles#\\MyApp\\keep", appbox::FilesystemIsolation::Whiteout),
                            layers, unmapped, error))
        << error;

    /* The isolation file of a patch package. */
    ASSERT_TRUE(table.Parse(IsolationFileOf("#ProgramFiles#\\MyApp", appbox::FilesystemIsolation::WriteCopy), layers,
                            unmapped, error))
        << error;
    ASSERT_TRUE(table.Parse(IsolationFileOf("#ProgramFiles#\\MyApp\\add", appbox::FilesystemIsolation::Whiteout),
                            layers, unmapped, error))
        << error;
    EXPECT_EQ(table.Count(), 3u);

    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::Full;
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::File;

    /* The mode of the path the patch names is the mode of the patch. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::WriteCopy);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::Directory);

    /* The mode of a path the patch does not name stays the mode below it. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\keep", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Whiteout);

    /* The mode of a path only the patch names is the mode of the patch. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\add", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Whiteout);

    /* A path which no file lists follows the closest entry above it. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\other\\file.txt", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::WriteCopy);
}

/**
 * @brief A document which cannot be used leaves the layers below it alone.
 *
 * The isolation file of a patch package is written by the user of the
 * application, so a file which is not a document of the schema must not drop
 * the modes of the archive it is applied on top of.
 */
TEST(Unit_FilesystemIsolation, ABrokenFileKeepsTheLayersBelowIt)
{
    const std::vector<appbox::filesystem::IsolationLayer> layers = {
        { L"#ProgramFiles#", L"\\??\\C:\\Program Files" },
    };

    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>          unmapped;
    std::string                        error;
    ASSERT_TRUE(table.Parse(IsolationFileOf("#ProgramFiles#\\MyApp", appbox::FilesystemIsolation::Full), layers,
                            unmapped, error))
        << error;

    EXPECT_FALSE(table.Parse("{ not a document", layers, unmapped, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(table.Count(), 1u);

    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::WriteCopy;
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::File;
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Full);
}

/**
 * @brief A path no entry covers follows no mode while the file lists none.
 *
 * The table answers with the root entry only when the document lists one, so a
 * sandbox whose file sets no root mode keeps the defaults of the view.
 */
TEST(Unit_FilesystemIsolation, ATableWithoutARootEntryAnswersNoUnlistedPath)
{
    const std::vector<appbox::filesystem::IsolationLayer> layers = {
        { L"#ProgramFiles#", L"\\??\\C:\\Program Files" },
    };

    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>          unmapped;
    std::string                        error;
    ASSERT_TRUE(table.Parse(IsolationFileOf("#ProgramFiles#\\MyApp", appbox::FilesystemIsolation::Merge), layers,
                            unmapped, error))
        << error;

    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::Full;
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::File;
    EXPECT_FALSE(table.Lookup(L"\\??\\C:\\Windows\\System32", mode, kind));
    EXPECT_FALSE(table.Lookup(L"\\??\\C:\\Program Files\\Other", mode, kind));

    /* A path below the listed folder still follows it. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\data", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Merge);
}

/**
 * @brief The stream name of a view path is read from its last component.
 *
 * A stream is addressed by the colon the last component carries, while the
 * colon of the drive names no stream: `\??\C:` is the drive and not a stream of
 * a file which is named `C`. A path which names no stream is returned as it is.
 */
TEST(Unit_FilesystemIsolation, StreamNamesAreReadFromTheLastComponent)
{
    using appbox::filesystem::CarriesStreamName;
    using appbox::filesystem::EntryPathOfStream;
    using appbox::filesystem::StreamNameOf;

    EXPECT_TRUE(CarriesStreamName(L"\\??\\C:\\dir\\file.txt:stream"));
    EXPECT_EQ(StreamNameOf(L"\\??\\C:\\dir\\file.txt:stream"), L"stream");
    EXPECT_EQ(EntryPathOfStream(L"\\??\\C:\\dir\\file.txt:stream"), L"\\??\\C:\\dir\\file.txt");

    /* The drive is not a stream, and a path without a stream keeps its name. */
    EXPECT_FALSE(CarriesStreamName(L"\\??\\C:"));
    EXPECT_FALSE(CarriesStreamName(L"\\??\\C:\\dir\\file.txt"));
    EXPECT_FALSE(CarriesStreamName(L"\\??\\C:\\dir\\"));
    EXPECT_EQ(EntryPathOfStream(L"\\??\\C:"), L"\\??\\C:");
    EXPECT_EQ(EntryPathOfStream(L"\\??\\C:\\dir\\file.txt"), L"\\??\\C:\\dir\\file.txt");

    /* Only the component of the drive is that short: a name of one letter
     * carries a stream as well. */
    EXPECT_TRUE(CarriesStreamName(L"\\??\\C:\\dir\\b:x"));
    EXPECT_EQ(EntryPathOfStream(L"\\??\\C:\\dir\\b:x"), L"\\??\\C:\\dir\\b");

    /* The stream is the last component, which also holds for a relative name. */
    EXPECT_TRUE(CarriesStreamName(L"file.txt:stream"));
    EXPECT_EQ(StreamNameOf(L"file.txt:stream"), L"stream");
    EXPECT_EQ(EntryPathOfStream(L"file.txt:stream"), L"file.txt");
}

/**
 * @brief The mode of a file covers the streams the file carries.
 *
 * A stream is the last component of its own path, so a lookup which only walks
 * the path upwards would reach the folder above the file and never the file
 * itself. The entry of the file is therefore probed as well, which is what
 * makes the mode of a file cover its streams; a path the file does not cover
 * keeps following the folder above it, and a document which names the stream
 * itself is more specific than the file and wins over it.
 */
TEST(Unit_FilesystemIsolation, AStreamFollowsTheFileWhichCarriesIt)
{
    const std::vector<appbox::filesystem::IsolationLayer> layers = {
        { L"#ProgramFiles#", L"\\??\\C:\\Program Files" },
    };

    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>          unmapped;
    std::string                        error;

    ASSERT_TRUE(table.Parse(IsolationFileOf("#ProgramFiles#\\MyApp", appbox::FilesystemIsolation::Full), layers,
                            unmapped, error))
        << error;
    ASSERT_TRUE(table.Parse(IsolationFileOf("#ProgramFiles#\\MyApp\\app.exe", appbox::FilesystemIsolation::Whiteout,
                                            appbox::FilesystemEntryKind::File),
                            layers, unmapped, error))
        << error;

    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::Full;
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::Directory;

    /* The mode and the kind of the file reach the streams it carries. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\app.exe:stream", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Whiteout);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::File);

    /* A stream of another file keeps following the folder above it. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\other.exe:stream", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Full);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::Directory);

    /* The file itself is decided by its own entry, as it was before. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\app.exe", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Whiteout);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::File);

    /* A document which names the stream itself is more specific than the file. */
    ASSERT_TRUE(table.Parse(IsolationFileOf("#ProgramFiles#\\MyApp\\app.exe:stream", appbox::FilesystemIsolation::Full,
                                            appbox::FilesystemEntryKind::File),
                            layers, unmapped, error))
        << error;
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\app.exe:stream", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Full);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::File);
}
