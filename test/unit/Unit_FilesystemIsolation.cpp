#include <gtest/gtest.h>
#include "filesystem/IsolationTable.hpp"
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
void SetMode(appbox::FilesystemIsolationModel& model, const std::wstring& path,
             appbox::FilesystemEntryKind kind, appbox::FilesystemIsolation isolation)
{
    std::string error;
    ASSERT_TRUE(model.SetIsolation(path, kind, isolation, error)) << error;
}

} // namespace

TEST(UnitFilesystemIsolation, NamesAreOrderedLikeTheEnumeration)
{
    const auto& names = appbox::FilesystemIsolationNames();
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], L"Full");
    EXPECT_EQ(names[1], L"Write Copy");
    EXPECT_EQ(names[2], L"Whiteout");

    EXPECT_EQ(appbox::FilesystemIsolationName(appbox::FilesystemIsolation::Full), L"Full");
    EXPECT_EQ(appbox::FilesystemIsolationName(appbox::FilesystemIsolation::WriteCopy), L"Write Copy");
    EXPECT_EQ(appbox::FilesystemIsolationName(appbox::FilesystemIsolation::Whiteout), L"Whiteout");
}

TEST(UnitFilesystemIsolation, NamesOfAKindDropTheFolderOnlyMode)
{
    const auto& folders = appbox::FilesystemIsolationNamesFor(appbox::FilesystemEntryKind::Directory);
    ASSERT_EQ(folders.size(), 3u);
    EXPECT_EQ(folders[0], L"Full");
    EXPECT_EQ(folders[1], L"Write Copy");
    EXPECT_EQ(folders[2], L"Whiteout");

    const auto& files = appbox::FilesystemIsolationNamesFor(appbox::FilesystemEntryKind::File);
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0], L"Full");
    EXPECT_EQ(files[1], L"Whiteout");
}

TEST(UnitFilesystemIsolation, ParseNameIgnoresTheCase)
{
    appbox::FilesystemIsolation isolation = appbox::FilesystemIsolation::Whiteout;

    EXPECT_TRUE(appbox::ParseFilesystemIsolationName(L"full", isolation));
    EXPECT_EQ(isolation, appbox::FilesystemIsolation::Full);

    EXPECT_TRUE(appbox::ParseFilesystemIsolationName(L"WRITE COPY", isolation));
    EXPECT_EQ(isolation, appbox::FilesystemIsolation::WriteCopy);

    EXPECT_TRUE(appbox::ParseFilesystemIsolationName(L"whiteout", isolation));
    EXPECT_EQ(isolation, appbox::FilesystemIsolation::Whiteout);

    EXPECT_FALSE(appbox::ParseFilesystemIsolationName(L"hide", isolation));
    EXPECT_FALSE(appbox::ParseFilesystemIsolationName(L"", isolation));
}

TEST(UnitFilesystemIsolation, TokensRoundTrip)
{
    for (const auto isolation :
         { appbox::FilesystemIsolation::Full, appbox::FilesystemIsolation::WriteCopy,
           appbox::FilesystemIsolation::Whiteout })
    {
        const std::string token = appbox::filesystem_isolation::IsolationToken(isolation);

        appbox::FilesystemIsolation parsed = appbox::FilesystemIsolation::Full;
        EXPECT_TRUE(appbox::filesystem_isolation::ParseIsolationToken(token, parsed));
        EXPECT_EQ(parsed, isolation);
    }

    appbox::FilesystemIsolation parsed = appbox::FilesystemIsolation::Full;
    EXPECT_TRUE(appbox::filesystem_isolation::ParseIsolationToken("Write-Copy", parsed));
    EXPECT_EQ(parsed, appbox::FilesystemIsolation::WriteCopy);
    EXPECT_TRUE(appbox::filesystem_isolation::ParseIsolationToken("writecopy", parsed));
    EXPECT_EQ(parsed, appbox::FilesystemIsolation::WriteCopy);
    EXPECT_FALSE(appbox::filesystem_isolation::ParseIsolationToken("hide", parsed));
}

TEST(UnitFilesystemIsolation, EntryKindTokensRoundTrip)
{
    EXPECT_STREQ(appbox::filesystem_isolation::EntryKindToken(appbox::FilesystemEntryKind::File), "file");
    EXPECT_STREQ(appbox::filesystem_isolation::EntryKindToken(appbox::FilesystemEntryKind::Directory),
                 "directory");

    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::File;
    EXPECT_TRUE(appbox::filesystem_isolation::ParseEntryKindToken("Directory", kind));
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::Directory);
    EXPECT_TRUE(appbox::filesystem_isolation::ParseEntryKindToken("folder", kind));
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::Directory);
    EXPECT_TRUE(appbox::filesystem_isolation::ParseEntryKindToken("FILE", kind));
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::File);
    EXPECT_FALSE(appbox::filesystem_isolation::ParseEntryKindToken("link", kind));
}

TEST(UnitFilesystemIsolation, OnlyAFolderAcceptsWriteCopy)
{
    using appbox::FilesystemEntryKind;
    using appbox::FilesystemIsolation;

    EXPECT_TRUE(appbox::filesystem_isolation::IsAllowed(FilesystemIsolation::Full, FilesystemEntryKind::File));
    EXPECT_FALSE(
        appbox::filesystem_isolation::IsAllowed(FilesystemIsolation::WriteCopy, FilesystemEntryKind::File));
    EXPECT_TRUE(
        appbox::filesystem_isolation::IsAllowed(FilesystemIsolation::Whiteout, FilesystemEntryKind::File));

    for (const auto isolation : { FilesystemIsolation::Full, FilesystemIsolation::WriteCopy,
                                  FilesystemIsolation::Whiteout })
    {
        EXPECT_TRUE(appbox::filesystem_isolation::IsAllowed(isolation, FilesystemEntryKind::Directory));
    }
}

TEST(UnitFilesystemIsolation, DefaultsDependOnTheKind)
{
    EXPECT_EQ(appbox::DefaultFilesystemIsolation(appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::WriteCopy);
    EXPECT_EQ(appbox::DefaultFilesystemIsolation(appbox::FilesystemEntryKind::File),
              appbox::FilesystemIsolation::Full);
}

TEST(UnitFilesystemIsolation, WriteCopyIsExpressedAsFullForAFile)
{
    using appbox::FilesystemEntryKind;
    using appbox::FilesystemIsolation;

    EXPECT_EQ(appbox::FilesystemIsolationForKind(FilesystemIsolation::WriteCopy, FilesystemEntryKind::File),
              FilesystemIsolation::Full);
    EXPECT_EQ(appbox::FilesystemIsolationForKind(FilesystemIsolation::Whiteout, FilesystemEntryKind::File),
              FilesystemIsolation::Whiteout);
    EXPECT_EQ(
        appbox::FilesystemIsolationForKind(FilesystemIsolation::WriteCopy, FilesystemEntryKind::Directory),
        FilesystemIsolation::WriteCopy);
}

TEST(UnitFilesystemIsolation, PathHelpersNormalizeAndSplit)
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

TEST(UnitFilesystemIsolation, PathComparisonIgnoresTheCase)
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

TEST(UnitFilesystemIsolation, FreshModelFollowsTheDefaults)
{
    appbox::FilesystemIsolationModel model;

    EXPECT_TRUE(model.IsEmpty());
    EXPECT_FALSE(model.HasExplicitIsolation(kAppFolder));
    EXPECT_EQ(model.EffectiveIsolation(kAppFolder, appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::WriteCopy);
    EXPECT_EQ(model.EffectiveIsolation(kAppFile, appbox::FilesystemEntryKind::File),
              appbox::FilesystemIsolation::Full);
}

TEST(UnitFilesystemIsolation, SetIsolationStoresOneEntry)
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

TEST(UnitFilesystemIsolation, SetIsolationUpdatesAnExistingEntry)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory,
            appbox::FilesystemIsolation::Whiteout);
    SetMode(model, L"#PROGRAMFILES#\\MYAPP", appbox::FilesystemEntryKind::Directory,
            appbox::FilesystemIsolation::Full);

    ASSERT_EQ(model.Entries().size(), 1u);
    EXPECT_EQ(model.Entries()[0].isolation, appbox::FilesystemIsolation::Full);
    /* The spelling the user picked first is kept. */
    EXPECT_EQ(model.Entries()[0].path, kAppFolder);
}

TEST(UnitFilesystemIsolation, SetIsolationRefusesInvalidInput)
{
    appbox::FilesystemIsolationModel model;
    std::string error;

    EXPECT_FALSE(model.SetIsolation(L"", appbox::FilesystemEntryKind::Directory,
                                    appbox::FilesystemIsolation::Full, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.SetIsolation(L"#ProgramFiles#\\..\\Windows", appbox::FilesystemEntryKind::Directory,
                                    appbox::FilesystemIsolation::Full, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.SetIsolation(kAppFile, appbox::FilesystemEntryKind::File,
                                    appbox::FilesystemIsolation::WriteCopy, error));
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("write_copy"), std::string::npos);

    /* A refused call never changes the model. */
    EXPECT_TRUE(model.IsEmpty());
}

TEST(UnitFilesystemIsolation, AChildOverridesTheFolderAbove)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory,
            appbox::FilesystemIsolation::Whiteout);

    /* Without a mode of its own the child follows the folder above it. */
    EXPECT_EQ(model.EffectiveIsolation(kDataFolder, appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Whiteout);

    SetMode(model, kDataFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    EXPECT_EQ(model.EffectiveIsolation(kDataFolder, appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Full);

    /* The folder above keeps its own mode, the entries below the child follow the child. */
    EXPECT_EQ(model.EffectiveIsolation(kAppFolder, appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Whiteout);
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#\\MyApp\\data\\logs",
                                       appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::Full);
}

TEST(UnitFilesystemIsolation, AWhiteoutFolderHidesTheEntriesBelowIt)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory,
            appbox::FilesystemIsolation::Whiteout);

    EXPECT_EQ(model.EffectiveIsolation(kAppFile, appbox::FilesystemEntryKind::File),
              appbox::FilesystemIsolation::Whiteout);
    EXPECT_EQ(model.EffectiveIsolation(L"#ProgramFiles#\\MyApp\\data\\settings.ini",
                                       appbox::FilesystemEntryKind::File),
              appbox::FilesystemIsolation::Whiteout);
}

TEST(UnitFilesystemIsolation, AMergedFolderShowsFullForAFile)
{
    appbox::FilesystemIsolationModel model;

    /* The default of a folder is `Write Copy`, which a file cannot express. */
    EXPECT_EQ(model.EffectiveIsolation(kAppFile, appbox::FilesystemEntryKind::File),
              appbox::FilesystemIsolation::Full);

    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory,
            appbox::FilesystemIsolation::Full);
    EXPECT_EQ(model.EffectiveIsolation(kAppFile, appbox::FilesystemEntryKind::File),
              appbox::FilesystemIsolation::Full);

    /* A file keeps its own mode even when a folder above it is hidden. */
    SetMode(model, kAppFile, appbox::FilesystemEntryKind::File, appbox::FilesystemIsolation::Whiteout);
    EXPECT_EQ(model.EffectiveIsolation(kAppFile, appbox::FilesystemEntryKind::File),
              appbox::FilesystemIsolation::Whiteout);
}

TEST(UnitFilesystemIsolation, RemoveSubtreeDropsTheEntryAndItsDescendants)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    SetMode(model, kDataFolder, appbox::FilesystemEntryKind::Directory,
            appbox::FilesystemIsolation::Whiteout);
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

TEST(UnitFilesystemIsolation, EntriesAreOrderedByPath)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, L"#Windows#", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    SetMode(model, L"#appdata#", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    SetMode(model, L"#Windows#\\System32", appbox::FilesystemEntryKind::Directory,
            appbox::FilesystemIsolation::Full);

    const auto& entries = model.Entries();
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].path, L"#appdata#");
    EXPECT_EQ(entries[1].path, L"#Windows#");
    EXPECT_EQ(entries[2].path, L"#Windows#\\System32");
}

TEST(UnitFilesystemIsolation, AddEntryRefusesDuplicatesAndInvalidModes)
{
    appbox::FilesystemIsolationModel model;
    std::string error;

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
}

TEST(UnitFilesystemIsolation, ResetDropsEveryMode)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory,
            appbox::FilesystemIsolation::Whiteout);

    model.Reset();

    EXPECT_TRUE(model.IsEmpty());
    EXPECT_FALSE(model.HasExplicitIsolation(kAppFolder));
    EXPECT_EQ(model.EffectiveIsolation(kAppFolder, appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::WriteCopy);
}

TEST(UnitFilesystemIsolation, BuildIsolationFileListsTheExplicitEntries)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    SetMode(model, kAppFile, appbox::FilesystemEntryKind::File, appbox::FilesystemIsolation::Whiteout);

    std::string text;
    std::string error;
    ASSERT_TRUE(appbox::BuildFilesystemIsolationFile(model, text, error)) << error;

    const auto document = nlohmann::json::parse(text);
    EXPECT_EQ(document[appbox::filesystem_isolation::kVersionKey].get<int>(),
              appbox::filesystem_isolation::kVersion);

    const auto& entries = document[appbox::filesystem_isolation::kEntriesKey];
    ASSERT_EQ(entries.size(), 2u);

    EXPECT_EQ(entries[0][appbox::filesystem_isolation::kPathKey].get<std::string>(), "#ProgramFiles#\\MyApp");
    EXPECT_EQ(entries[0][appbox::filesystem_isolation::kKindKey].get<std::string>(), "directory");
    EXPECT_EQ(entries[0][appbox::filesystem_isolation::kIsolationKey].get<std::string>(), "full");

    EXPECT_EQ(entries[1][appbox::filesystem_isolation::kPathKey].get<std::string>(),
              "#ProgramFiles#\\MyApp\\app.exe");
    EXPECT_EQ(entries[1][appbox::filesystem_isolation::kKindKey].get<std::string>(), "file");
    EXPECT_EQ(entries[1][appbox::filesystem_isolation::kIsolationKey].get<std::string>(), "whiteout");
}

TEST(UnitFilesystemIsolation, BuildIsolationFileOfAModelWithoutModes)
{
    appbox::FilesystemIsolationModel model;

    std::string text;
    std::string error;
    ASSERT_TRUE(appbox::BuildFilesystemIsolationFile(model, text, error)) << error;

    const auto document = nlohmann::json::parse(text);
    EXPECT_EQ(document[appbox::filesystem_isolation::kVersionKey].get<int>(),
              appbox::filesystem_isolation::kVersion);
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
TEST(UnitFilesystemIsolation, TheIsolationFileOfThePackerIsReadByTheSandbox)
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
    std::vector<std::wstring>            unmapped;
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
