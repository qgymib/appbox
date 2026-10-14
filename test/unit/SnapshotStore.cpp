#include <gtest/gtest.h>
#include "src/core/SnapshotStore.hpp"
#include "utils/SnapshotFile.hpp"
#include "WString.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{

/**
 * @brief Generate a unique name fragment for temporary folders.
 * @return The unique fragment.
 */
std::wstring UniqueFragment()
{
    static unsigned counter = 0;
    const auto      ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_wstring(ticks) + L"-" + std::to_wstring(++counter);
}

/**
 * @brief RAII helper creating a unique folder below the temp directory.
 */
class TempDir
{
public:
    TempDir()
    {
        const auto base = std::filesystem::temp_directory_path();
        path_ = base / (L"appbox-snapshot-store-" + UniqueFragment());
        std::filesystem::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /**
     * @brief Get the folder path.
     * @return The folder path.
     */
    const std::filesystem::path& Get() const
    {
        return path_;
    }

    /**
     * @brief Get the path of a file inside the folder.
     * @param[in] name Name of the file.
     * @return The path of the file.
     */
    std::wstring File(const wchar_t* name) const
    {
        return (path_ / name).wstring();
    }

private:
    std::filesystem::path path_;
};

/**
 * @brief Build the entries of a phase which exercise every part of the schema.
 * @return The entries of the phase.
 */
appbox::test::SnapshotPhaseContent SamplePhase()
{
    appbox::test::SnapshotPhaseContent content;

    content.files = {
        { "program_files", L"Foo",                                  true,  0,    132000000000000000 },
        { "program_files", L"Foo\\Bar.exe",                         false, 4096, 132000000000000001 },
        { "user_profile",  L"\u00e4\u00f6\u00fc\\\u4e2d\u6587.txt", false, 12,   132000000000000002 },
    };

    content.registry_keys = { L"HKEY_CURRENT_USER", L"HKEY_CURRENT_USER\\Software\\Foo" };

    appbox::SnapshotRegistryValueEntry text;
    text.path = L"HKEY_CURRENT_USER\\Software\\Foo";
    text.name = L"Server";
    text.type = 1;
    text.data = { 0x68, 0x00, 0x69, 0x00, 0x00, 0x00 };
    content.registry_values.push_back(text);

    appbox::SnapshotRegistryValueEntry binary;
    binary.path = L"HKEY_CURRENT_USER\\Software\\Foo";
    binary.name = L"Blob";
    binary.type = 3;
    binary.data = { 0x00, 0xFF, 0x80, 0x00, 0x7F };
    content.registry_values.push_back(binary);

    /*
     * A value which holds no byte is what a capture of a real machine reports
     * for an empty value, so the sample carries one.
     */
    appbox::SnapshotRegistryValueEntry empty;
    empty.path = L"HKEY_CURRENT_USER\\Software\\Foo";
    empty.name = L"Empty";
    empty.type = 0;
    content.registry_values.push_back(empty);

    content.environment = {
        { L"APPBOX_TEST", L"C:\\Program Files\\Foo" },
        { L"Empty",       L""                       }
    };

    return content;
}

/**
 * @brief Create a snapshot file which names a schema version of its own.
 * @param[in] path Path of the file to create.
 * @param[in] version Version to store in the file.
 * @return true when the file was created.
 */
bool WriteForeignVersionFile(const std::wstring& path, int version)
{
    sqlite3* db = nullptr;
    if (sqlite3_open16(path.c_str(), &db) != SQLITE_OK)
    {
        return false;
    }

    const auto sql = std::string("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
                                 "INSERT INTO meta (key, value) VALUES ('version', '") +
                     std::to_string(version) + "');";
    const auto status = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    sqlite3_close(db);

    return status == SQLITE_OK;
}

/**
 * @brief Read the storage class and the length of the data of the only
 *        registry value a snapshot file holds.
 *
 * The storage class is what SQLite reports for the column, which is what tells
 * a value of no bytes (`blob` of length zero) from a value which was stored as
 * the absence of a value (`null`).
 *
 * @param[in] path Path of the snapshot file.
 * @param[out] storage_class Storage class of the column.
 * @param[out] length Number of bytes of the column.
 * @return true when the file holds exactly one registry value which was read.
 */
bool ReadStoredValueData(const std::wstring& path, std::string& storage_class, int& length)
{
    sqlite3* db = nullptr;
    if (sqlite3_open16(path.c_str(), &db) != SQLITE_OK)
    {
        sqlite3_close(db);
        return false;
    }

    sqlite3_stmt* statement = nullptr;
    bool          read = false;
    if (sqlite3_prepare_v2(db, "SELECT typeof(data), length(data) FROM registry_values", -1, &statement, nullptr) ==
        SQLITE_OK)
    {
        if (sqlite3_step(statement) == SQLITE_ROW)
        {
            const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
            storage_class = text != nullptr ? text : "";
            length = sqlite3_column_int(statement, 1);
            read = true;
        }
    }

    sqlite3_finalize(statement);
    sqlite3_close(db);
    return read;
}

} // namespace

TEST(Unit_SnapshotStore, RoundTripsEveryPartOfAPhase)
{
    const TempDir dir;
    const auto    path = dir.File(L"snapshot.db");

    const auto sample = SamplePhase();

    std::string error;
    ASSERT_TRUE(appbox::test::WriteSnapshotPhase(path, appbox::SnapshotPhase::Before, sample, error)) << error;

    appbox::test::SnapshotPhaseContent loaded;
    ASSERT_TRUE(appbox::test::ReadSnapshotPhase(path, appbox::SnapshotPhase::Before, loaded, error)) << error;

    ASSERT_EQ(loaded.files.size(), sample.files.size());
    for (const auto& entry : sample.files)
    {
        const auto* found = appbox::test::FindFileEntry(loaded, entry.path);
        ASSERT_NE(found, nullptr) << appbox::WideToUTF8(entry.path);
        EXPECT_EQ(found->preset_id, entry.preset_id);
        EXPECT_EQ(found->directory, entry.directory);
        EXPECT_EQ(found->size, entry.size);
        EXPECT_EQ(found->write_time, entry.write_time);
    }

    auto expected_keys = sample.registry_keys;
    std::sort(expected_keys.begin(), expected_keys.end());
    EXPECT_EQ(loaded.registry_keys, expected_keys);

    ASSERT_EQ(loaded.registry_values.size(), sample.registry_values.size());
    for (const auto& value : sample.registry_values)
    {
        const auto* found = appbox::test::FindRegistryValueEntry(loaded, value.path, value.name);
        ASSERT_NE(found, nullptr) << appbox::WideToUTF8(value.name);
        EXPECT_EQ(found->type, value.type);
        EXPECT_EQ(found->data, value.data);
    }

    ASSERT_EQ(loaded.environment.size(), sample.environment.size());
    for (const auto& variable : sample.environment)
    {
        const auto found = std::find_if(
            loaded.environment.begin(), loaded.environment.end(),
            [&variable](const appbox::SnapshotEnvironmentEntry& entry) { return entry.name == variable.name; });
        ASSERT_NE(found, loaded.environment.end()) << appbox::WideToUTF8(variable.name);
        EXPECT_EQ(found->value, variable.value);
    }
}

TEST(Unit_SnapshotStore, RoundTripsARegistryValueWhichHoldsNoBytes)
{
    const TempDir dir;
    const auto    path = dir.File(L"no-bytes.db");

    appbox::test::SnapshotPhaseContent content;
    appbox::SnapshotRegistryValueEntry empty;
    empty.path = L"HKEY_CURRENT_USER\\Software\\Foo";
    empty.name = L"Empty";
    empty.type = 0;
    content.registry_keys = { empty.path };
    content.registry_values.push_back(empty);

    std::string error;
    ASSERT_TRUE(appbox::test::WriteSnapshotPhase(path, appbox::SnapshotPhase::Before, content, error)) << error;

    /*
     * A value which holds no byte is a blob of length zero: storing it as the
     * absence of a value would lose the value and the column refuses it.
     */
    std::string storage_class;
    int         length = -1;
    ASSERT_TRUE(ReadStoredValueData(path, storage_class, length));
    EXPECT_EQ(storage_class, "blob");
    EXPECT_EQ(length, 0);

    const auto reader = appbox::SnapshotReader::Open(path, error);
    ASSERT_NE(reader, nullptr) << error;

    appbox::SnapshotRegistryValueEntry loaded;
    bool                               found = false;
    ASSERT_TRUE(reader->FindRegistryValue(appbox::SnapshotPhase::Before, empty.path, empty.name, loaded, found, error))
        << error;
    ASSERT_TRUE(found);
    EXPECT_EQ(loaded.type, empty.type);
    EXPECT_TRUE(loaded.data.empty());
}

TEST(Unit_SnapshotStore, KeepsTheTwoPhasesOfAFileApart)
{
    const TempDir dir;
    const auto    path = dir.File(L"two-phases.db");

    appbox::test::SnapshotPhaseContent before;
    before.files.push_back({ "program_files", L"Foo\\baseline.exe", false, 1, 1 });

    appbox::test::SnapshotPhaseContent after;
    after.files.push_back({ "program_files", L"Foo\\installed.exe", false, 2, 2 });

    std::string error;
    ASSERT_TRUE(appbox::test::WriteSnapshotFile(path, before, after, error)) << error;

    appbox::test::SnapshotPhaseContent loaded_before;
    appbox::test::SnapshotPhaseContent loaded_after;
    ASSERT_TRUE(appbox::test::ReadSnapshotPhase(path, appbox::SnapshotPhase::Before, loaded_before, error)) << error;
    ASSERT_TRUE(appbox::test::ReadSnapshotPhase(path, appbox::SnapshotPhase::After, loaded_after, error)) << error;

    ASSERT_EQ(loaded_before.files.size(), 1u);
    EXPECT_EQ(loaded_before.files[0].path, L"Foo\\baseline.exe");

    ASSERT_EQ(loaded_after.files.size(), 1u);
    EXPECT_EQ(loaded_after.files[0].path, L"Foo\\installed.exe");
}

TEST(Unit_SnapshotStore, ReplacesThePhaseAfterTheChange)
{
    const TempDir dir;
    const auto    path = dir.File(L"again.db");

    appbox::test::SnapshotPhaseContent before;
    before.files.push_back({ "program_files", L"Foo\\baseline.exe", false, 1, 1 });

    appbox::test::SnapshotPhaseContent first;
    first.files.push_back({ "program_files", L"Foo\\first.exe", false, 2, 2 });

    appbox::test::SnapshotPhaseContent second;
    second.files.push_back({ "program_files", L"Foo\\second.exe", false, 3, 3 });

    std::string error;
    ASSERT_TRUE(appbox::test::WriteSnapshotFile(path, before, first, error)) << error;
    ASSERT_TRUE(appbox::test::WriteSnapshotPhase(path, appbox::SnapshotPhase::After, second, error)) << error;

    appbox::test::SnapshotPhaseContent loaded_after;
    ASSERT_TRUE(appbox::test::ReadSnapshotPhase(path, appbox::SnapshotPhase::After, loaded_after, error)) << error;
    ASSERT_EQ(loaded_after.files.size(), 1u);
    EXPECT_EQ(loaded_after.files[0].path, L"Foo\\second.exe");

    /* The baseline of the file is not part of the replacement. */
    appbox::test::SnapshotPhaseContent loaded_before;
    ASSERT_TRUE(appbox::test::ReadSnapshotPhase(path, appbox::SnapshotPhase::Before, loaded_before, error)) << error;
    ASSERT_EQ(loaded_before.files.size(), 1u);
    EXPECT_EQ(loaded_before.files[0].path, L"Foo\\baseline.exe");
}

TEST(Unit_SnapshotStore, ClearsBothPhasesWhenTheBaselineIsWritten)
{
    const TempDir dir;
    const auto    path = dir.File(L"new-baseline.db");

    appbox::test::SnapshotPhaseContent before;
    before.files.push_back({ "program_files", L"Foo\\baseline.exe", false, 1, 1 });

    appbox::test::SnapshotPhaseContent after;
    after.files.push_back({ "program_files", L"Foo\\installed.exe", false, 2, 2 });

    std::string error;
    ASSERT_TRUE(appbox::test::WriteSnapshotFile(path, before, after, error)) << error;

    /*
     * A baseline which replaces the baseline of the file must not leave the
     * state of an earlier comparison behind.
     */
    appbox::test::SnapshotPhaseContent other;
    other.files.push_back({ "program_files", L"Foo\\other.exe", false, 3, 3 });
    ASSERT_TRUE(appbox::test::WriteSnapshotPhase(path, appbox::SnapshotPhase::Before, other, error)) << error;

    const auto reader = appbox::SnapshotReader::Open(path, error);
    ASSERT_NE(reader, nullptr) << error;

    bool before_present = false;
    ASSERT_TRUE(reader->HasPhase(appbox::SnapshotPhase::Before, before_present, error)) << error;
    EXPECT_TRUE(before_present);

    bool after_present = true;
    ASSERT_TRUE(reader->HasPhase(appbox::SnapshotPhase::After, after_present, error)) << error;
    EXPECT_FALSE(after_present);

    appbox::test::SnapshotPhaseContent loaded;
    ASSERT_TRUE(appbox::test::ReadSnapshotPhase(path, appbox::SnapshotPhase::Before, loaded, error)) << error;
    ASSERT_EQ(loaded.files.size(), 1u);
    EXPECT_EQ(loaded.files[0].path, L"Foo\\other.exe");
}

TEST(Unit_SnapshotStore, ReportsAPhaseWhichWasNotCommittedAsAbsent)
{
    const TempDir dir;
    const auto    path = dir.File(L"cancelled.db");

    {
        std::string error;
        const auto  writer = appbox::SnapshotWriter::Create(path, error);
        ASSERT_NE(writer, nullptr) << error;
        ASSERT_TRUE(writer->BeginPhase(appbox::SnapshotPhase::After, error)) << error;
        ASSERT_TRUE(writer->WriteFile({ "program_files", L"Foo\\half.exe", false, 1, 1 }, error)) << error;

        /* A run which the user cancelled ends its phase without marking it. */
        writer->AbandonPhase();
    }

    std::string error;
    const auto  reader = appbox::SnapshotReader::Open(path, error);
    ASSERT_NE(reader, nullptr) << error;

    bool present = true;
    ASSERT_TRUE(reader->HasPhase(appbox::SnapshotPhase::After, present, error)) << error;
    EXPECT_FALSE(present);

    /* The rows are cleared by the next run which writes the phase. */
    appbox::test::SnapshotPhaseContent content;
    content.files.push_back({ "program_files", L"Foo\\whole.exe", false, 2, 2 });
    ASSERT_TRUE(appbox::test::WriteSnapshotPhase(path, appbox::SnapshotPhase::After, content, error)) << error;

    appbox::test::SnapshotPhaseContent loaded;
    ASSERT_TRUE(appbox::test::ReadSnapshotPhase(path, appbox::SnapshotPhase::After, loaded, error)) << error;
    ASSERT_EQ(loaded.files.size(), 1u);
    EXPECT_EQ(loaded.files[0].path, L"Foo\\whole.exe");
}

TEST(Unit_SnapshotStore, FindsAnEntryByItsFoldedName)
{
    const TempDir dir;
    const auto    path = dir.File(L"lookup.db");

    appbox::test::SnapshotPhaseContent content;
    content.files.push_back({ "program_files", L"Foo\\Bar.exe", false, 1, 1 });
    content.registry_keys = { L"HKEY_CURRENT_USER\\Software\\Foo" };
    content.registry_values.push_back({
        L"HKEY_CURRENT_USER\\Software\\Foo", L"Server", 1, { 1, 2 }
    });
    content.environment = {
        { L"APPBOX_TEST", L"1" }
    };

    std::string error;
    ASSERT_TRUE(appbox::test::WriteSnapshotPhase(path, appbox::SnapshotPhase::Before, content, error)) << error;

    const auto reader = appbox::SnapshotReader::Open(path, error);
    ASSERT_NE(reader, nullptr) << error;

    appbox::SnapshotFileEntry file;
    bool                      found = false;
    ASSERT_TRUE(reader->FindFile(appbox::SnapshotPhase::Before, "program_files", L"foo\\BAR.EXE", file, found, error))
        << error;
    EXPECT_TRUE(found);
    EXPECT_EQ(file.path, L"Foo\\Bar.exe");

    std::wstring key;
    ASSERT_TRUE(
        reader->FindRegistryKey(appbox::SnapshotPhase::Before, L"hkey_current_user\\SOFTWARE\\foo", key, found, error))
        << error;
    EXPECT_TRUE(found);
    EXPECT_EQ(key, L"HKEY_CURRENT_USER\\Software\\Foo");

    appbox::SnapshotRegistryValueEntry value;
    ASSERT_TRUE(reader->FindRegistryValue(appbox::SnapshotPhase::Before, L"HKEY_CURRENT_USER\\Software\\Foo", L"server",
                                          value, found, error))
        << error;
    EXPECT_TRUE(found);
    EXPECT_EQ(value.data, (std::vector<std::uint8_t>{ 1, 2 }));

    appbox::SnapshotEnvironmentEntry variable;
    ASSERT_TRUE(reader->FindEnvironment(appbox::SnapshotPhase::Before, L"appbox_test", variable, found, error))
        << error;
    EXPECT_TRUE(found);
    EXPECT_EQ(variable.value, L"1");

    /* An entry which the phase does not hold is reported as absent. */
    ASSERT_TRUE(reader->FindFile(appbox::SnapshotPhase::Before, "program_files", L"Foo\\Gone.exe", file, found, error))
        << error;
    EXPECT_FALSE(found);

    ASSERT_TRUE(reader->FindEnvironment(appbox::SnapshotPhase::Before, L"APPBOX_MISSING", variable, found, error))
        << error;
    EXPECT_FALSE(found);
}

TEST(Unit_SnapshotStore, CountsTheEntriesOfAPhase)
{
    const TempDir dir;
    const auto    path = dir.File(L"counts.db");

    std::string error;
    ASSERT_TRUE(appbox::test::WriteSnapshotPhase(path, appbox::SnapshotPhase::Before, SamplePhase(), error)) << error;

    const auto reader = appbox::SnapshotReader::Open(path, error);
    ASSERT_NE(reader, nullptr) << error;

    appbox::SnapshotPhaseCounts counts;
    ASSERT_TRUE(reader->Counts(appbox::SnapshotPhase::Before, counts, error)) << error;
    EXPECT_EQ(counts.files, 3u);
    EXPECT_EQ(counts.registry_keys, 2u);
    EXPECT_EQ(counts.registry_values, 3u);
    EXPECT_EQ(counts.environment, 2u);

    /* A phase which was never written holds nothing. */
    ASSERT_TRUE(reader->Counts(appbox::SnapshotPhase::After, counts, error)) << error;
    EXPECT_EQ(counts.files, 0u);
    EXPECT_EQ(counts.registry_keys, 0u);
    EXPECT_EQ(counts.registry_values, 0u);
    EXPECT_EQ(counts.environment, 0u);
}

TEST(Unit_SnapshotStore, CopiesTheBaselineIntoAnotherFile)
{
    const TempDir dir;
    const auto    source = dir.File(L"source.db");
    const auto    copy = dir.File(L"copy.db");

    appbox::test::SnapshotPhaseContent before;
    before.files.push_back({ "program_files", L"Foo\\baseline.exe", false, 1, 1 });
    before.environment = {
        { L"APPBOX_TEST", L"1" }
    };

    appbox::test::SnapshotPhaseContent after;
    after.files.push_back({ "program_files", L"Foo\\installed.exe", false, 2, 2 });

    std::string error;
    ASSERT_TRUE(appbox::test::WriteSnapshotFile(source, before, after, error)) << error;
    ASSERT_TRUE(appbox::CopySnapshotPhase(source, appbox::SnapshotPhase::Before, copy, error)) << error;

    appbox::test::SnapshotPhaseContent loaded;
    ASSERT_TRUE(appbox::test::ReadSnapshotPhase(copy, appbox::SnapshotPhase::Before, loaded, error)) << error;
    ASSERT_EQ(loaded.files.size(), 1u);
    EXPECT_EQ(loaded.files[0].path, L"Foo\\baseline.exe");
    ASSERT_EQ(loaded.environment.size(), 1u);
    EXPECT_EQ(loaded.environment[0].name, L"APPBOX_TEST");

    /* The copy is a baseline of its own: the state after the change stays out. */
    const auto reader = appbox::SnapshotReader::Open(copy, error);
    ASSERT_NE(reader, nullptr) << error;

    bool present = true;
    ASSERT_TRUE(reader->HasPhase(appbox::SnapshotPhase::After, present, error)) << error;
    EXPECT_FALSE(present);
}

TEST(Unit_SnapshotStore, RejectsACopyOfAPhaseWhichIsAbsent)
{
    const TempDir dir;
    const auto    source = dir.File(L"only-before.db");
    const auto    copy = dir.File(L"copy.db");

    std::string error;
    ASSERT_TRUE(appbox::test::WriteSnapshotPhase(source, appbox::SnapshotPhase::Before, SamplePhase(), error)) << error;

    EXPECT_FALSE(appbox::CopySnapshotPhase(source, appbox::SnapshotPhase::After, copy, error));
    EXPECT_FALSE(error.empty());

    /* A copy which fails leaves no file behind. */
    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(copy), ec));
    EXPECT_FALSE(ec);
}

TEST(Unit_SnapshotStore, RejectsAFileWhichIsNotASnapshot)
{
    const TempDir dir;
    const auto    path = dir.File(L"text.db");

    {
        std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << "this is not a snapshot file";
    }

    std::string error;
    EXPECT_EQ(appbox::SnapshotReader::Open(path, error), nullptr);
    EXPECT_FALSE(error.empty());

    /*
     * The file may be the baseline of a session, so a writer which cannot open
     * it leaves it alone.
     */
    EXPECT_EQ(appbox::SnapshotWriter::Open(path, error), nullptr);
    EXPECT_FALSE(error.empty());

    std::error_code ec;
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(path), ec));
    EXPECT_FALSE(ec);
}

TEST(Unit_SnapshotStore, RejectsAVersionWhichIsNotSupported)
{
    const TempDir dir;
    const auto    path = dir.File(L"future.db");

    ASSERT_TRUE(WriteForeignVersionFile(path, appbox::kSnapshotSchemaVersion + 1));

    std::string error;
    EXPECT_EQ(appbox::SnapshotReader::Open(path, error), nullptr);
    EXPECT_NE(error.find("version"), std::string::npos) << error;

    EXPECT_EQ(appbox::SnapshotWriter::Open(path, error), nullptr);
    EXPECT_NE(error.find("version"), std::string::npos) << error;
}

TEST(Unit_SnapshotStore, RejectsAMissingFileWithoutCreatingIt)
{
    const TempDir dir;
    const auto    path = dir.File(L"missing.db");

    std::string error;
    EXPECT_EQ(appbox::SnapshotReader::Open(path, error), nullptr);
    EXPECT_FALSE(error.empty());

    EXPECT_EQ(appbox::SnapshotWriter::Open(path, error), nullptr);
    EXPECT_FALSE(error.empty());

    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(path), ec));
}

TEST(Unit_SnapshotStore, RejectsAnEmptyPath)
{
    const TempDir dir;
    const auto    path = dir.File(L"snapshot.db");

    std::string error;
    EXPECT_EQ(appbox::SnapshotWriter::Create(L"", error), nullptr);
    EXPECT_FALSE(error.empty());

    EXPECT_EQ(appbox::SnapshotWriter::Open(L"", error), nullptr);
    EXPECT_FALSE(error.empty());

    EXPECT_EQ(appbox::SnapshotReader::Open(L"", error), nullptr);
    EXPECT_FALSE(error.empty());

    EXPECT_FALSE(appbox::CopySnapshotPhase(L"", appbox::SnapshotPhase::Before, path, error));
    EXPECT_FALSE(error.empty());
}

TEST(Unit_SnapshotStore, RemovesTheFileWhenAWriteFails)
{
    const TempDir dir;
    const auto    path = dir.File(L"failed.db");

    /*
     * Two entries which name the same folder of a preset directory collide in
     * the primary key of the table, so the write fails after the file was
     * created and the database was opened.
     */
    std::string error;
    const auto  writer = appbox::SnapshotWriter::Create(path, error);
    ASSERT_NE(writer, nullptr) << error;
    ASSERT_TRUE(writer->BeginPhase(appbox::SnapshotPhase::Before, error)) << error;
    ASSERT_TRUE(writer->WriteFile({ "program_files", L"Foo", true, 0, 0 }, error)) << error;
    EXPECT_FALSE(writer->WriteFile({ "program_files", L"Foo", true, 0, 0 }, error));
    EXPECT_FALSE(error.empty());

    /* A half written baseline is of no use, so it must not stay behind. */
    EXPECT_TRUE(writer->DiscardFile());

    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(path), ec));
    EXPECT_FALSE(ec);
}

TEST(Unit_SnapshotStore, ReportsAFailureWhenTheFolderDoesNotExist)
{
    const TempDir dir;
    const auto    path = (dir.Get() / L"no-such-folder" / L"snapshot.db").wstring();

    std::string error;
    EXPECT_EQ(appbox::SnapshotWriter::Create(path, error), nullptr);
    EXPECT_FALSE(error.empty());

    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(path), ec));
}
