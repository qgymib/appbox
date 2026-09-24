#include "filesystem/IsolationTable.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

/**
 * @brief The layers used by the cases below: two known folders and a drive.
 * @return The layers in mapping order.
 */
std::vector<appbox::filesystem::IsolationLayer> Layers()
{
    return {
        { L"#ProgramFiles#", L"\\??\\C:\\Program Files" },
        { L"#APPDATA#", L"\\??\\C:\\Users\\foo\\AppData\\Roaming" },
        { L"C", L"\\??\\C:" },
    };
}

/**
 * @brief View paths of the imported folder of the cases below.
 * @{
 */
constexpr wchar_t kMyApp[] = L"\\??\\C:\\Program Files\\MyApp";
constexpr wchar_t kAppFile[] = L"\\??\\C:\\Program Files\\MyApp\\app.exe";
constexpr wchar_t kDataFolder[] = L"\\??\\C:\\Program Files\\MyApp\\data";
constexpr wchar_t kSettingsFile[] = L"\\??\\C:\\Program Files\\MyApp\\data\\settings.ini";
/**
 * @}
 */

} // namespace

/**
 * @brief A listed virtual path is translated into the view path of its layer.
 */
TEST(UnitFilesystemIsolationTable, ParseTranslatesVirtualPaths)
{
    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>            unmapped;
    std::string                          error;

    const std::string text = R"({
        "version": 1,
        "entries": [
            { "path": "#ProgramFiles#\\MyApp", "kind": "directory", "isolation": "full" },
            { "path": "#APPDATA#\\app.ini", "kind": "file", "isolation": "whiteout" }
        ]
    })";

    ASSERT_TRUE(table.Parse(text, Layers(), unmapped, error)) << error;
    EXPECT_TRUE(unmapped.empty());
    EXPECT_FALSE(table.Empty());
    EXPECT_EQ(table.Count(), 2u);

    appbox::FilesystemIsolation   mode = appbox::FilesystemIsolation::WriteCopy;
    appbox::FilesystemEntryKind   kind = appbox::FilesystemEntryKind::File;

    ASSERT_TRUE(table.Lookup(kMyApp, mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Full);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::Directory);

    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Users\\foo\\AppData\\Roaming\\app.ini", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Whiteout);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::File);
}

/**
 * @brief The closest listed entry covers its whole subtree.
 */
TEST(UnitFilesystemIsolationTable, LookupWalksUpToTheClosestEntry)
{
    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>            unmapped;
    std::string                          error;

    const std::string text = R"({
        "version": 1,
        "entries": [
            { "path": "#ProgramFiles#\\MyApp", "kind": "directory", "isolation": "full" },
            { "path": "#ProgramFiles#\\MyApp\\data", "kind": "directory", "isolation": "write_copy" }
        ]
    })";

    ASSERT_TRUE(table.Parse(text, Layers(), unmapped, error)) << error;

    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::WriteCopy;
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::File;

    /* A path below the folder follows the folder above it. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\bin\\tool.exe", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Full);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::Directory);

    /* The folder below overrides the folder above for its own subtree. */
    ASSERT_TRUE(table.Lookup(kSettingsFile, mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::WriteCopy);
    EXPECT_EQ(kind, appbox::FilesystemEntryKind::Directory);

    /* The entry itself is decided by its own mode as well. */
    ASSERT_TRUE(table.Lookup(kDataFolder, mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::WriteCopy);

    /* A path which no entry covers has no listed mode. */
    EXPECT_FALSE(table.Lookup(L"\\??\\C:\\Windows\\notepad.exe", mode, kind));
    EXPECT_FALSE(table.Lookup(L"", mode, kind));
}

/**
 * @brief The comparison ignores the case and the trailing separator.
 */
TEST(UnitFilesystemIsolationTable, LookupIgnoresTheCaseAndTrailingSeparators)
{
    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>            unmapped;
    std::string                          error;

    const std::string text = R"({
        "version": 1,
        "entries": [
            { "path": "#programfiles#\\myapp\\", "kind": "directory", "isolation": "whiteout" }
        ]
    })";

    ASSERT_TRUE(table.Parse(text, Layers(), unmapped, error)) << error;

    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::Full;
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::File;

    ASSERT_TRUE(table.Lookup(L"\\??\\c:\\PROGRAM FILES\\MYAPP", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Whiteout);

    /* The separated form of the same path is the same key. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\MyApp\\app.exe", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Whiteout);
}

/**
 * @brief A layer whose key is a drive letter covers the drive and its root.
 */
TEST(UnitFilesystemIsolationTable, ADriveLayerCoversTheDriveRoot)
{
    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>            unmapped;
    std::string                          error;

    const std::string text = R"({
        "version": 1,
        "entries": [
            { "path": "C", "kind": "directory", "isolation": "full" }
        ]
    })";

    ASSERT_TRUE(table.Parse(text, Layers(), unmapped, error)) << error;

    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::WriteCopy;
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::File;

    /* The drive root keeps its separator in the view, but not in the key. */
    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Full);

    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Windows\\notepad.exe", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Full);
}

/**
 * @brief An entry whose layer is not mapped is reported and skipped.
 */
TEST(UnitFilesystemIsolationTable, EntriesWithoutALayerAreSkipped)
{
    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>            unmapped;
    std::string                          error;

    const std::string text = R"({
        "version": 1,
        "entries": [
            { "path": "#NETWORK#\\share", "kind": "directory", "isolation": "full" },
            { "path": "#ProgramFiles#\\MyApp", "kind": "directory", "isolation": "full" },
            { "path": "#ProgramFiles#\\..\\Windows", "kind": "directory", "isolation": "full" }
        ]
    })";

    ASSERT_TRUE(table.Parse(text, Layers(), unmapped, error)) << error;
    EXPECT_EQ(table.Count(), 1u);

    ASSERT_EQ(unmapped.size(), 2u);
    EXPECT_EQ(unmapped[0], L"#NETWORK#\\share");
    EXPECT_EQ(unmapped[1], L"#ProgramFiles#\\..\\Windows");

    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::Whiteout;
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::File;
    ASSERT_TRUE(table.Lookup(kMyApp, mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Full);
}

/**
 * @brief An entry which is listed twice keeps the mode of the last one.
 */
TEST(UnitFilesystemIsolationTable, TheLastEntryOfAPathWins)
{
    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>            unmapped;
    std::string                          error;

    const std::string text = R"({
        "version": 1,
        "entries": [
            { "path": "#ProgramFiles#\\MyApp", "kind": "directory", "isolation": "full" },
            { "path": "#ProgramFiles#\\MyApp", "kind": "directory", "isolation": "write_copy" }
        ]
    })";

    ASSERT_TRUE(table.Parse(text, Layers(), unmapped, error)) << error;
    EXPECT_EQ(table.Count(), 1u);

    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::Full;
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::File;
    ASSERT_TRUE(table.Lookup(kMyApp, mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::WriteCopy);
}

/**
 * @brief A document without an entry list is a valid empty table.
 */
TEST(UnitFilesystemIsolationTable, ADocumentWithoutEntriesIsEmpty)
{
    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>            unmapped;
    std::string                          error;

    ASSERT_TRUE(table.Parse(R"({ "version": 1 })", Layers(), unmapped, error)) << error;
    EXPECT_TRUE(table.Empty());
    EXPECT_EQ(table.Count(), 0u);
}

/**
 * @brief Every malformed document is rejected with a description.
 */
TEST(UnitFilesystemIsolationTable, MalformedDocumentsAreRejected)
{
    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>            unmapped;
    std::string                          error;

    const std::vector<std::string> documents = {
        "not json at all",
        R"([ "an array" ])",
        R"({ "version": 2, "entries": [] })",
        R"({ "version": 1, "entries": 7 })",
        R"({ "version": 1, "entries": [ 7 ] })",
        R"({ "version": 1, "entries": [ { "kind": "file", "isolation": "full" } ] })",
        R"({ "version": 1, "entries": [ { "path": "", "kind": "file", "isolation": "full" } ] })",
        R"({ "version": 1, "entries": [ { "path": "#ProgramFiles#\\a", "kind": "link", "isolation": "full" } ] })",
        R"({ "version": 1, "entries": [ { "path": "#ProgramFiles#\\a", "kind": "file", "isolation": "hide" } ] })",
        R"({ "version": 1, "entries": [ { "path": "#ProgramFiles#\\a", "kind": "file", "isolation": "write_copy" } ] })",
        R"({ "version": 1, "entries": [ { "path": 7, "kind": "file", "isolation": "full" } ] })",
    };

    for (const auto& document : documents)
    {
        error.clear();
        EXPECT_FALSE(table.Parse(document, Layers(), unmapped, error)) << document;
        EXPECT_FALSE(error.empty()) << document;
        EXPECT_TRUE(table.Empty()) << document;
    }
}

/**
 * @brief A rejected document leaves the modes of the previous one in place.
 */
TEST(UnitFilesystemIsolationTable, AFailedParseKeepsTheLoadedModes)
{
    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>            unmapped;
    std::string                          error;

    const std::string text = R"({
        "version": 1,
        "entries": [ { "path": "#ProgramFiles#\\MyApp", "kind": "directory", "isolation": "full" } ]
    })";

    ASSERT_TRUE(table.Parse(text, Layers(), unmapped, error)) << error;
    ASSERT_EQ(table.Count(), 1u);

    EXPECT_FALSE(table.Parse(R"({ "version": 1, "entries": 7 })", Layers(), unmapped, error));
    EXPECT_EQ(table.Count(), 1u);

    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::WriteCopy;
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::File;
    ASSERT_TRUE(table.Lookup(kMyApp, mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Full);
}

/**
 * @brief The layer key is the name of the layer folder of the base filesystem.
 */
TEST(UnitFilesystemIsolationTable, LayerKeyOfAMappedFolder)
{
    using appbox::filesystem::IsolationTable;

    EXPECT_EQ(IsolationTable::LayerKeyOf(L"\\??\\D:\\Sandbox\\filesystem\\#ProgramFiles#"), L"#ProgramFiles#");
    EXPECT_EQ(IsolationTable::LayerKeyOf(L"\\??\\D:\\Sandbox\\filesystem\\#APPDATA#\\"), L"#APPDATA#");
    EXPECT_EQ(IsolationTable::LayerKeyOf(L"C"), L"C");
    EXPECT_EQ(IsolationTable::LayerKeyOf(L""), L"");
}

/**
 * @brief The normalized form drops empty components and the trailing separator.
 */
TEST(UnitFilesystemIsolationTable, NormalizeViewPath)
{
    using appbox::filesystem::IsolationTable;

    EXPECT_EQ(IsolationTable::NormalizeViewPath(L"\\??\\C:\\Program Files\\MyApp\\"),
              L"\\??\\C:\\Program Files\\MyApp");
    EXPECT_EQ(IsolationTable::NormalizeViewPath(L"\\??\\C:\\"), L"\\??\\C:");
    EXPECT_EQ(IsolationTable::NormalizeViewPath(L"\\??\\C:\\Program Files\\.\\MyApp"),
              L"\\??\\C:\\Program Files\\MyApp");
    EXPECT_EQ(IsolationTable::NormalizeViewPath(L"\\??\\C:\\Program Files/MyApp"),
              L"\\??\\C:\\Program Files\\MyApp");

    /* A parent reference would leave the virtual filesystem. */
    EXPECT_TRUE(IsolationTable::NormalizeViewPath(L"\\??\\C:\\..\\Windows").empty());
    EXPECT_TRUE(IsolationTable::NormalizeViewPath(L"").empty());
}

/**
 * @brief The keys of the table are ordered ignoring the case.
 */
TEST(UnitFilesystemIsolationTable, EntriesAreOrderedIgnoringTheCase)
{
    appbox::filesystem::IsolationTable table;
    std::vector<std::wstring>            unmapped;
    std::string                          error;

    const std::string text = R"({
        "version": 1,
        "entries": [
            { "path": "#ProgramFiles#\\MyApp", "kind": "directory", "isolation": "full" },
            { "path": "#ProgramFiles#", "kind": "directory", "isolation": "whiteout" }
        ]
    })";

    ASSERT_TRUE(table.Parse(text, Layers(), unmapped, error)) << error;
    EXPECT_EQ(table.Count(), 2u);

    /* The nearer entry wins even when it was listed first. */
    appbox::FilesystemIsolation mode = appbox::FilesystemIsolation::WriteCopy;
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::File;
    ASSERT_TRUE(table.Lookup(kAppFile, mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Full);

    ASSERT_TRUE(table.Lookup(L"\\??\\C:\\Program Files\\Other", mode, kind));
    EXPECT_EQ(mode, appbox::FilesystemIsolation::Whiteout);
}
