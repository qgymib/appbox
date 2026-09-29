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
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], L"Full");
    EXPECT_EQ(names[1], L"Write Copy");
    EXPECT_EQ(names[2], L"Whiteout");

    EXPECT_EQ(appbox::FilesystemIsolationName(appbox::FilesystemIsolation::Full), L"Full");
    EXPECT_EQ(appbox::FilesystemIsolationName(appbox::FilesystemIsolation::WriteCopy), L"Write Copy");
    EXPECT_EQ(appbox::FilesystemIsolationName(appbox::FilesystemIsolation::Whiteout), L"Whiteout");
}

TEST(Unit_FilesystemIsolation, NamesOfAKindDropTheFolderOnlyMode)
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

TEST(Unit_FilesystemIsolation, ParseNameIgnoresTheCase)
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

TEST(Unit_FilesystemIsolation, TokensRoundTrip)
{
    for (const auto isolation : { appbox::FilesystemIsolation::Full, appbox::FilesystemIsolation::WriteCopy,
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

TEST(Unit_FilesystemIsolation, OnlyAFolderAcceptsWriteCopy)
{
    using appbox::FilesystemEntryKind;
    using appbox::FilesystemIsolation;

    EXPECT_TRUE(appbox::filesystem_isolation::IsAllowed(FilesystemIsolation::Full, FilesystemEntryKind::File));
    EXPECT_FALSE(appbox::filesystem_isolation::IsAllowed(FilesystemIsolation::WriteCopy, FilesystemEntryKind::File));
    EXPECT_TRUE(appbox::filesystem_isolation::IsAllowed(FilesystemIsolation::Whiteout, FilesystemEntryKind::File));

    for (const auto isolation :
         { FilesystemIsolation::Full, FilesystemIsolation::WriteCopy, FilesystemIsolation::Whiteout })
    {
        EXPECT_TRUE(appbox::filesystem_isolation::IsAllowed(isolation, FilesystemEntryKind::Directory));
    }
}

TEST(Unit_FilesystemIsolation, DefaultsDependOnTheKind)
{
    EXPECT_EQ(appbox::DefaultFilesystemIsolation(appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::WriteCopy);
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
              appbox::FilesystemIsolation::WriteCopy);
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

    EXPECT_FALSE(
        model.SetIsolation(L"", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.SetIsolation(L"#ProgramFiles#\\..\\Windows", appbox::FilesystemEntryKind::Directory,
                                    appbox::FilesystemIsolation::Full, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(
        model.SetIsolation(kAppFile, appbox::FilesystemEntryKind::File, appbox::FilesystemIsolation::WriteCopy, error));
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("write_copy"), std::string::npos);

    /* A refused call never changes the model. */
    EXPECT_TRUE(model.IsEmpty());
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

    /* The default of a folder is `Write Copy`, which a file cannot express. */
    EXPECT_EQ(model.EffectiveIsolation(kAppFile, appbox::FilesystemEntryKind::File), appbox::FilesystemIsolation::Full);

    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Full);
    EXPECT_EQ(model.EffectiveIsolation(kAppFile, appbox::FilesystemEntryKind::File), appbox::FilesystemIsolation::Full);

    /* A file keeps its own mode even when a folder above it is hidden. */
    SetMode(model, kAppFile, appbox::FilesystemEntryKind::File, appbox::FilesystemIsolation::Whiteout);
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

    const auto& entries = model.Entries();
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].path, L"#appdata#");
    EXPECT_EQ(entries[1].path, L"#Windows#");
    EXPECT_EQ(entries[2].path, L"#Windows#\\System32");
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
}

TEST(Unit_FilesystemIsolation, ResetDropsEveryMode)
{
    appbox::FilesystemIsolationModel model;
    SetMode(model, kAppFolder, appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Whiteout);

    model.Reset();

    EXPECT_TRUE(model.IsEmpty());
    EXPECT_FALSE(model.HasExplicitIsolation(kAppFolder));
    EXPECT_EQ(model.EffectiveIsolation(kAppFolder, appbox::FilesystemEntryKind::Directory),
              appbox::FilesystemIsolation::WriteCopy);
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
 * @brief Every mode of the table is explained by a description.
 *
 * The description is the text the workspace shows for the mode of a row and for
 * the modes the `Isolation` column offers, so every mode has to name itself and
 * its own rule.
 */
TEST(Unit_FilesystemIsolation, EveryModeIsDescribedForATooltip)
{
    for (const auto isolation : { appbox::FilesystemIsolation::Full, appbox::FilesystemIsolation::WriteCopy,
                                  appbox::FilesystemIsolation::Whiteout })
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
    EXPECT_NE(appbox::FilesystemIsolationDescription(appbox::FilesystemIsolation::WriteCopy,
                                                     appbox::FilesystemEntryKind::Directory)
                  .find(L"default mode of a folder"),
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
 * @return The UTF-8 text of the document.
 */
static std::string IsolationFileOf(const std::string& path, appbox::FilesystemIsolation isolation)
{
    appbox::filesystem_isolation::Entry entry;
    entry.path = path;
    entry.kind = appbox::FilesystemEntryKind::Directory;
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
