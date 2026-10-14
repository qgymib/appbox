#include "SnapshotStore.hpp"
#include "WString.hpp"
#include <sqlite3.h>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

/* Keys of the meta table. */
constexpr const char* kMetaVersionKey = "version";
constexpr const char* kMetaBeforeKey = "before_created_utc";
constexpr const char* kMetaAfterKey = "after_created_utc";

/* Columns of the four parts of a phase, in the order the decoders read them. */
constexpr const char* kFileColumns = "preset, path, directory, size, write_time";
constexpr const char* kRegistryKeyColumns = "path";
constexpr const char* kRegistryValueColumns = "path, name, type, data";
constexpr const char* kEnvironmentColumns = "name, value";

/* The four tables which carry the entries of a phase. */
constexpr const char* kTables[] = { "filesystem", "registry_keys", "registry_values", "environment" };

/**
 * @brief Owns the handle of an open SQLite database.
 */
struct Database
{
    /**
     * @brief Handle of the database, null while none is open.
     */
    sqlite3* handle = nullptr;

    /**
     * @brief Close the database unless it was moved away.
     */
    ~Database()
    {
        Close();
    }

    /**
     * @brief Close the database before the owner goes out of scope.
     *
     * The call is what lets the file of the database be removed again: SQLite
     * opens the file without sharing it for deletion, so a remove of a file
     * which is still open fails on Windows.
     */
    void Close()
    {
        if (handle != nullptr && sqlite3_close(handle) == SQLITE_OK)
        {
            handle = nullptr;
        }
    }

    Database() = default;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
};

/**
 * @brief Owns one prepared statement of an open database.
 */
struct Statement
{
    /**
     * @brief Handle of the statement, null while none is prepared.
     */
    sqlite3_stmt* handle = nullptr;

    /**
     * @brief Finalize the statement unless it was moved away.
     */
    ~Statement()
    {
        Finalize();
    }

    /**
     * @brief Release the statement before the database is closed.
     *
     * SQLite refuses to close a database which still holds a prepared
     * statement, and a database which is still open keeps its file locked, so
     * a caller which removes the file releases the statements first.
     */
    void Finalize()
    {
        if (handle != nullptr)
        {
            sqlite3_finalize(handle);
            handle = nullptr;
        }
    }

    Statement() = default;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
};

/**
 * @brief Format the error of the last failed statement of a database.
 * @param[in] db The database.
 * @param[in] context Description of the failed operation.
 * @return The composed error description.
 */
std::string ErrorText(sqlite3* db, const char* context)
{
    const char* message = db != nullptr ? sqlite3_errmsg(db) : nullptr;
    return std::string(context) + ": " + (message != nullptr ? message : "no database handle");
}

/**
 * @brief Run one statement which returns no result.
 * @param[in] db The database.
 * @param[in] sql The statement to run.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool Execute(sqlite3* db, const char* sql, std::string& error)
{
    char*      message = nullptr;
    const auto status = sqlite3_exec(db, sql, nullptr, nullptr, &message);
    if (status != SQLITE_OK)
    {
        error = std::string("the snapshot database rejected '") + sql +
                "': " + (message != nullptr ? message : sqlite3_errmsg(db));
        if (message != nullptr)
        {
            sqlite3_free(message);
        }
        return false;
    }

    return true;
}

/**
 * @brief Compile one statement.
 * @param[in] db The database.
 * @param[in] sql The statement to compile.
 * @param[out] statement The compiled statement.
 * @param[in] context Description of the failed operation.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool Prepare(sqlite3* db, const char* sql, Statement& statement, const char* context, std::string& error)
{
    if (sqlite3_prepare_v2(db, sql, -1, &statement.handle, nullptr) != SQLITE_OK)
    {
        error = ErrorText(db, context);
        return false;
    }

    return true;
}

/**
 * @brief Run a statement which is expected to finish without a result row.
 * @param[in] db The database.
 * @param[in] statement The statement to run.
 * @param[in] context Description of the failed operation.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool StepDone(sqlite3* db, Statement& statement, const char* context, std::string& error)
{
    if (sqlite3_step(statement.handle) != SQLITE_DONE)
    {
        error = ErrorText(db, context);
        return false;
    }

    return true;
}

/**
 * @brief Bind a wide text as UTF-16 to a parameter of a statement.
 * @param[in] statement The statement.
 * @param[in] index One based index of the parameter.
 * @param[in] text The text to bind.
 */
void BindText(sqlite3_stmt* statement, int index, const std::wstring& text)
{
    sqlite3_bind_text16(statement, index, text.data(), static_cast<int>(text.size() * sizeof(wchar_t)),
                        SQLITE_TRANSIENT);
}

/**
 * @brief Bind a narrow text as UTF-8 to a parameter of a statement.
 * @param[in] statement The statement.
 * @param[in] index One based index of the parameter.
 * @param[in] text The text to bind.
 */
void BindText(sqlite3_stmt* statement, int index, const std::string& text)
{
    sqlite3_bind_text(statement, index, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT);
}

/**
 * @brief Bind the raw bytes of a registry value to a parameter of a statement.
 *
 * A value which holds no byte is bound as a blob of length zero. Handing the
 * null pointer of an empty vector to `sqlite3_bind_blob()` would bind the
 * absence of a value instead, which a column the schema declares as not null
 * refuses, so the two cases are told apart here.
 *
 * @param[in] statement The statement.
 * @param[in] index One based index of the parameter.
 * @param[in] data The bytes to bind.
 */
void BindBlob(sqlite3_stmt* statement, int index, const std::vector<std::uint8_t>& data)
{
    if (data.empty())
    {
        sqlite3_bind_zeroblob(statement, index, 0);
        return;
    }

    sqlite3_bind_blob(statement, index, data.data(), static_cast<int>(data.size()), SQLITE_TRANSIENT);
}

/**
 * @brief Read a column which holds UTF-16 text.
 * @param[in] statement The statement.
 * @param[in] index Zero based index of the column.
 * @return The text of the column, empty when the column is null.
 */
std::wstring ColumnText16(sqlite3_stmt* statement, int index)
{
    const auto* text = static_cast<const wchar_t*>(sqlite3_column_text16(statement, index));
    if (text == nullptr)
    {
        return {};
    }

    const auto bytes = sqlite3_column_bytes16(statement, index);
    if (bytes <= 0)
    {
        return {};
    }

    return std::wstring(text, static_cast<std::size_t>(bytes) / sizeof(wchar_t));
}

/**
 * @brief Read a column which holds UTF-8 text.
 * @param[in] statement The statement.
 * @param[in] index Zero based index of the column.
 * @return The text of the column, empty when the column is null.
 */
std::string ColumnText8(sqlite3_stmt* statement, int index)
{
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, index));
    if (text == nullptr)
    {
        return {};
    }

    const auto bytes = sqlite3_column_bytes(statement, index);
    if (bytes <= 0)
    {
        return {};
    }

    return std::string(text, static_cast<std::size_t>(bytes));
}

/**
 * @brief Read a column which holds the raw bytes of a registry value.
 * @param[in] statement The statement.
 * @param[in] index Zero based index of the column.
 * @return The bytes of the column, empty when the column is null.
 */
std::vector<std::uint8_t> ColumnBlob(sqlite3_stmt* statement, int index)
{
    const auto* data = static_cast<const std::uint8_t*>(sqlite3_column_blob(statement, index));
    const auto  bytes = sqlite3_column_bytes(statement, index);
    if (data == nullptr || bytes <= 0)
    {
        return {};
    }

    return std::vector<std::uint8_t>(data, data + bytes);
}

/**
 * @brief Get the number a phase is stored under.
 * @param[in] phase The phase.
 * @return The number of the phase.
 */
int PhaseValue(appbox::SnapshotPhase phase)
{
    return phase == appbox::SnapshotPhase::Before ? 0 : 1;
}

/**
 * @brief Get the meta key which marks one phase.
 * @param[in] phase The phase.
 * @return The key of the marker.
 */
const char* PhaseMetaKey(appbox::SnapshotPhase phase)
{
    return phase == appbox::SnapshotPhase::Before ? kMetaBeforeKey : kMetaAfterKey;
}

/**
 * @brief Describe one phase inside an error message.
 * @param[in] phase The phase.
 * @return The description of the phase.
 */
const char* PhaseText(appbox::SnapshotPhase phase)
{
    return phase == appbox::SnapshotPhase::Before ? "the state before the change" : "the state after the change";
}

/**
 * @brief Build the statement which reads the entries of one part of a phase.
 * @param[in] table Name of the table of the part.
 * @param[in] columns Columns of the part.
 * @param[in] by_key Whether the statement looks up one entry instead of walking the part.
 * @return The statement.
 */
std::string SelectSql(const char* table, const char* columns, bool by_key)
{
    std::string sql = "SELECT ";
    sql += columns;
    sql += " FROM ";
    sql += table;
    sql += " WHERE phase = ?";
    sql += by_key ? " AND lookup = ?" : " ORDER BY lookup";
    return sql;
}

/**
 * @brief Read one meta entry of a snapshot file.
 *
 * A file which holds no meta table at all is not a snapshot file, so the query
 * itself fails and the failure is reported with the description the caller
 * passed.
 *
 * @param[in] db The database.
 * @param[in] key Name of the entry to read.
 * @param[in] context Description of the failed operation.
 * @param[out] value Value of the entry, empty when it is absent.
 * @param[out] found Whether the file holds the entry.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool ReadMeta(sqlite3* db, const char* key, const char* context, std::string& value, bool& found, std::string& error)
{
    Statement statement;
    if (!Prepare(db, "SELECT value FROM meta WHERE key = ?", statement, context, error))
    {
        return false;
    }

    BindText(statement.handle, 1, std::string(key));

    const auto status = sqlite3_step(statement.handle);
    if (status == SQLITE_ROW)
    {
        value = ColumnText8(statement.handle, 0);
        found = true;
        return true;
    }
    if (status != SQLITE_DONE)
    {
        error = ErrorText(db, context);
        return false;
    }

    /* An absent entry is not a failure: the caller decides what it means. */
    value.clear();
    found = false;
    return true;
}

/**
 * @brief Create the schema of a snapshot file.
 *
 * A snapshot is written once and can be captured again at any time, so the
 * journal and the synchronous writes of the database are turned off: that is
 * what keeps the write of a snapshot of a whole machine in the range of
 * seconds instead of minutes. The trade is that the database cannot roll a
 * phase back, which is why a phase counts as a state only once its marker was
 * written (see `SnapshotWriter::CommitPhase`).
 *
 * @param[in] db The database.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool CreateSchema(sqlite3* db, std::string& error)
{
    return Execute(db, "PRAGMA journal_mode = OFF", error) && Execute(db, "PRAGMA synchronous = OFF", error) &&
           Execute(db, "CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)", error) &&
           Execute(
               db,
               "CREATE TABLE filesystem (phase INTEGER NOT NULL, lookup TEXT NOT NULL, preset TEXT NOT NULL, "
               "path TEXT NOT NULL, directory INTEGER NOT NULL, size INTEGER NOT NULL, write_time INTEGER NOT NULL, "
               "PRIMARY KEY (phase, lookup))",
               error) &&
           Execute(db,
                   "CREATE TABLE registry_keys (phase INTEGER NOT NULL, lookup TEXT NOT NULL, path TEXT NOT NULL, "
                   "PRIMARY KEY (phase, lookup))",
                   error) &&
           Execute(db,
                   "CREATE TABLE registry_values (phase INTEGER NOT NULL, lookup TEXT NOT NULL, path TEXT NOT NULL, "
                   "name TEXT NOT NULL, type INTEGER NOT NULL, data BLOB NOT NULL, PRIMARY KEY (phase, lookup))",
                   error) &&
           Execute(db,
                   "CREATE TABLE environment (phase INTEGER NOT NULL, lookup TEXT NOT NULL, name TEXT NOT NULL, "
                   "value TEXT NOT NULL, PRIMARY KEY (phase, lookup))",
                   error) &&
           Execute(db,
                   (std::string("INSERT INTO meta (key, value) VALUES ('") + kMetaVersionKey + "', '" +
                    std::to_string(appbox::kSnapshotSchemaVersion) + "')")
                       .c_str(),
                   error);
}

/**
 * @brief Open a snapshot file and check the schema version it carries.
 * @param[in] path Path of the file.
 * @param[in] create Whether a new file is created instead of an existing one opened.
 * @param[out] database The database, closed when the call fails.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool OpenDatabase(const std::wstring& path, bool create, Database& database, std::string& error)
{
    if (sqlite3_open16(path.c_str(), &database.handle) != SQLITE_OK)
    {
        error = ErrorText(database.handle, "cannot open the snapshot file");
        return false;
    }

    if (create)
    {
        if (!CreateSchema(database.handle, error))
        {
            return false;
        }
    }
    else if (!Execute(database.handle, "PRAGMA synchronous = OFF", error))
    {
        return false;
    }

    std::string version;
    bool        found = false;
    if (!ReadMeta(database.handle, kMetaVersionKey, "the file is not an AppBox snapshot", version, found, error))
    {
        return false;
    }
    if (!found)
    {
        error = "the file is not an AppBox snapshot: it carries no schema version";
        return false;
    }

    int version_value = 0;
    try
    {
        version_value = std::stoi(version);
    }
    catch (const std::exception&)
    {
        error = "the snapshot file carries an unreadable schema version: '" + version + "'";
        return false;
    }
    if (version_value != appbox::kSnapshotSchemaVersion)
    {
        error = "unsupported snapshot file version " + std::to_string(version_value) + " (expected " +
                std::to_string(appbox::kSnapshotSchemaVersion) + ")";
        return false;
    }

    return true;
}

/**
 * @brief Remove the entries of one phase, or of both phases.
 * @param[in] db The database.
 * @param[in] phase The phase which is about to be written.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool DeletePhaseRows(sqlite3* db, appbox::SnapshotPhase phase, std::string& error)
{
    /*
     * A capture of the state before a change replaces the baseline of the
     * file, so the state of an earlier comparison must not survive it.
     */
    if (phase == appbox::SnapshotPhase::Before)
    {
        for (const auto* table : kTables)
        {
            if (!Execute(db, (std::string("DELETE FROM ") + table).c_str(), error))
            {
                return false;
            }
        }

        return Execute(
            db,
            (std::string("DELETE FROM meta WHERE key IN ('") + kMetaBeforeKey + "', '" + kMetaAfterKey + "')").c_str(),
            error);
    }

    const auto value = std::to_string(PhaseValue(phase));
    for (const auto* table : kTables)
    {
        if (!Execute(db, (std::string("DELETE FROM ") + table + " WHERE phase = " + value).c_str(), error))
        {
            return false;
        }
    }

    return Execute(db, (std::string("DELETE FROM meta WHERE key = '") + kMetaAfterKey + "'").c_str(), error);
}

/**
 * @brief Read one entry of the filesystem part of a row.
 * @param[in] statement The statement which holds the row.
 * @param[out] entry The entry to fill.
 */
void DecodeFile(sqlite3_stmt* statement, appbox::SnapshotFileEntry& entry)
{
    entry.preset_id = ColumnText8(statement, 0);
    entry.path = ColumnText16(statement, 1);
    entry.directory = sqlite3_column_int(statement, 2) != 0;
    entry.size = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 3));
    entry.write_time = static_cast<std::int64_t>(sqlite3_column_int64(statement, 4));
}

/**
 * @brief Read one key of the registry part of a row.
 * @param[in] statement The statement which holds the row.
 * @param[out] path The path of the key.
 */
void DecodeRegistryKey(sqlite3_stmt* statement, std::wstring& path)
{
    path = ColumnText16(statement, 0);
}

/**
 * @brief Read one value of the registry part of a row.
 * @param[in] statement The statement which holds the row.
 * @param[out] entry The value to fill.
 */
void DecodeRegistryValue(sqlite3_stmt* statement, appbox::SnapshotRegistryValueEntry& entry)
{
    entry.path = ColumnText16(statement, 0);
    entry.name = ColumnText16(statement, 1);
    entry.type = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 2));
    entry.data = ColumnBlob(statement, 3);
}

/**
 * @brief Read one variable of the environment part of a row.
 * @param[in] statement The statement which holds the row.
 * @param[out] entry The variable to fill.
 */
void DecodeEnvironment(sqlite3_stmt* statement, appbox::SnapshotEnvironmentEntry& entry)
{
    entry.name = ColumnText16(statement, 0);
    entry.value = ColumnText16(statement, 1);
}

/**
 * @brief Visit every row a query yields for one phase.
 *
 * @param[in] db The database.
 * @param[in] sql The query, which takes the phase and no further parameter.
 * @param[in] context Description of the failed operation.
 * @param[in] phase The phase to read.
 * @param[in] decode Fills one entry from the current row.
 * @param[in] visit Callback invoked per entry; false stops the walk.
 * @param[out] error Error description when the query fails.
 * @return false when the query failed.
 */
template <typename Entry, typename Decode, typename Visit>
bool ForEachEntry(sqlite3* db, const std::string& sql, const char* context, appbox::SnapshotPhase phase,
                  const Decode& decode, const Visit& visit, std::string& error)
{
    Statement statement;
    if (!Prepare(db, sql.c_str(), statement, context, error))
    {
        return false;
    }

    sqlite3_bind_int(statement.handle, 1, PhaseValue(phase));

    for (;;)
    {
        const auto status = sqlite3_step(statement.handle);
        if (status == SQLITE_DONE)
        {
            return true;
        }
        if (status != SQLITE_ROW)
        {
            error = ErrorText(db, context);
            return false;
        }

        Entry entry;
        decode(statement.handle, entry);
        if (!visit(entry))
        {
            return true;
        }
    }
}

/**
 * @brief Look up one row of a query for one phase.
 *
 * @param[in] db The database.
 * @param[in] sql The query, which takes the phase and the identifier.
 * @param[in] context Description of the failed operation.
 * @param[in] phase The phase to read.
 * @param[in] lookup Identifier of the entry.
 * @param[in] decode Fills the entry from the current row.
 * @param[out] entry The entry which was found.
 * @param[out] found Whether the phase holds the entry.
 * @param[out] error Error description when the query fails.
 * @return false when the query failed.
 */
template <typename Entry, typename Decode>
bool FindEntry(sqlite3* db, const std::string& sql, const char* context, appbox::SnapshotPhase phase,
               const std::wstring& lookup, const Decode& decode, Entry& entry, bool& found, std::string& error)
{
    Statement statement;
    if (!Prepare(db, sql.c_str(), statement, context, error))
    {
        return false;
    }

    sqlite3_bind_int(statement.handle, 1, PhaseValue(phase));
    BindText(statement.handle, 2, lookup);

    const auto status = sqlite3_step(statement.handle);
    if (status == SQLITE_DONE)
    {
        found = false;
        return true;
    }
    if (status != SQLITE_ROW)
    {
        error = ErrorText(db, context);
        return false;
    }

    decode(statement.handle, entry);
    found = true;
    return true;
}

/**
 * @brief Count the rows of one table which belong to one phase.
 * @param[in] db The database.
 * @param[in] table Name of the table.
 * @param[in] phase The phase to count.
 * @param[out] count Number of the rows of the phase.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool CountRows(sqlite3* db, const char* table, appbox::SnapshotPhase phase, std::size_t& count, std::string& error)
{
    const auto sql = std::string("SELECT COUNT(*) FROM ") + table + " WHERE phase = ?";

    Statement statement;
    if (!Prepare(db, sql.c_str(), statement, "failed to count the entries of the snapshot", error))
    {
        return false;
    }

    sqlite3_bind_int(statement.handle, 1, PhaseValue(phase));
    if (sqlite3_step(statement.handle) != SQLITE_ROW)
    {
        error = ErrorText(db, "failed to count the entries of the snapshot");
        return false;
    }

    count = static_cast<std::size_t>(sqlite3_column_int64(statement.handle, 0));
    return true;
}

} // namespace

namespace appbox
{

/**
 * @brief The state of a writer which is not part of its interface.
 */
struct SnapshotWriter::Impl
{
    std::wstring  path;                          ///< Path of the snapshot file.
    Database      database;                      ///< The open database.
    SnapshotPhase phase = SnapshotPhase::Before; ///< Phase which is being written.
    bool          in_phase = false;              ///< Whether a phase is being written.

    Statement meta;            ///< Writes the marker of a phase.
    Statement filesystem;      ///< Writes one filesystem entry.
    Statement registry_keys;   ///< Writes one registry key.
    Statement registry_values; ///< Writes one registry value.
    Statement environment;     ///< Writes one environment variable.

    /**
     * @brief Release the statements before the database is closed.
     */
    void FinalizeStatements()
    {
        meta.Finalize();
        filesystem.Finalize();
        registry_keys.Finalize();
        registry_values.Finalize();
        environment.Finalize();
    }
};

/**
 * @brief The state of a reader which is not part of its interface.
 */
struct SnapshotReader::Impl
{
    std::wstring path;     ///< Path of the snapshot file.
    Database     database; ///< The open database.
};

SnapshotWriter::SnapshotWriter() : impl_(new Impl())
{
}

bool SnapshotWriter::PrepareStatements(std::string& error)
{
    auto* db = impl_->database.handle;

    /*
     * The entries of the environment replace the entry of the same name: the
     * variables of the current user win over the variables of the machine,
     * which is the precedence the operating system applies, and the two keys
     * are read one after the other.
     */
    return Prepare(db, "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)", impl_->meta,
                   "failed to prepare the marker of a phase", error) &&
           Prepare(db,
                   "INSERT INTO filesystem (phase, lookup, preset, path, directory, size, write_time) "
                   "VALUES (?,?,?,?,?,?,?)",
                   impl_->filesystem, "failed to prepare a filesystem entry", error) &&
           Prepare(db, "INSERT INTO registry_keys (phase, lookup, path) VALUES (?,?,?)", impl_->registry_keys,
                   "failed to prepare a registry key", error) &&
           Prepare(db, "INSERT INTO registry_values (phase, lookup, path, name, type, data) VALUES (?,?,?,?,?,?)",
                   impl_->registry_values, "failed to prepare a registry value", error) &&
           Prepare(db, "INSERT OR REPLACE INTO environment (phase, lookup, name, value) VALUES (?,?,?,?)",
                   impl_->environment, "failed to prepare an environment variable", error);
}

SnapshotWriter::~SnapshotWriter()
{
    /* A phase which the caller never ended is abandoned instead of rolled back. */
    AbandonPhase();
}

std::unique_ptr<SnapshotWriter> SnapshotWriter::Create(const std::wstring& path, std::string& error)
{
    if (path.empty())
    {
        error = "no snapshot file path was given";
        return nullptr;
    }

    /*
     * The destination is replaced as a whole: an existing file is removed
     * first, because SQLite would otherwise open it with the schema it already
     * holds and refuse to create the tables a second time.
     */
    {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(path), ec);
    }

    auto writer = std::unique_ptr<SnapshotWriter>(new SnapshotWriter());
    writer->impl_->path = path;

    if (!OpenDatabase(path, true, writer->impl_->database, error) || !writer->PrepareStatements(error))
    {
        /* A half written snapshot is of no use, so it must not stay behind. */
        writer->DiscardFile();
        return nullptr;
    }

    return writer;
}

std::unique_ptr<SnapshotWriter> SnapshotWriter::Open(const std::wstring& path, std::string& error)
{
    if (path.empty())
    {
        error = "no snapshot file path was given";
        return nullptr;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::path(path), ec) || ec)
    {
        error = "cannot open the snapshot file: " + WideToUTF8(path);
        return nullptr;
    }

    auto writer = std::unique_ptr<SnapshotWriter>(new SnapshotWriter());
    writer->impl_->path = path;

    /*
     * The file is the baseline of the session, so it is never removed here: a
     * file which cannot be written is reported to the caller instead.
     */
    if (!OpenDatabase(path, false, writer->impl_->database, error) || !writer->PrepareStatements(error))
    {
        return nullptr;
    }

    return writer;
}

bool SnapshotWriter::BeginPhase(SnapshotPhase phase, std::string& error)
{
    if (impl_->in_phase)
    {
        error = "the snapshot writer is already writing a phase";
        return false;
    }

    auto* db = impl_->database.handle;
    if (!Execute(db, "BEGIN IMMEDIATE", error))
    {
        return false;
    }

    /* The phase is open from here on, so every failure path ends it. */
    impl_->phase = phase;
    impl_->in_phase = true;

    return DeletePhaseRows(db, phase, error);
}

bool SnapshotWriter::WriteFile(const SnapshotFileEntry& entry, std::string& error)
{
    if (!impl_->in_phase)
    {
        error = "no phase of the snapshot is being written";
        return false;
    }

    auto& statement = impl_->filesystem;
    sqlite3_reset(statement.handle);
    sqlite3_bind_int(statement.handle, 1, PhaseValue(impl_->phase));
    BindText(statement.handle, 2, snapshot::FileLookupKey(entry.preset_id, entry.path));
    BindText(statement.handle, 3, entry.preset_id);
    BindText(statement.handle, 4, entry.path);
    sqlite3_bind_int(statement.handle, 5, entry.directory ? 1 : 0);
    sqlite3_bind_int64(statement.handle, 6, static_cast<sqlite3_int64>(entry.size));
    sqlite3_bind_int64(statement.handle, 7, static_cast<sqlite3_int64>(entry.write_time));

    return StepDone(impl_->database.handle, statement, "failed to write a filesystem entry", error);
}

bool SnapshotWriter::WriteRegistryKey(const std::wstring& path, std::string& error)
{
    if (!impl_->in_phase)
    {
        error = "no phase of the snapshot is being written";
        return false;
    }

    auto& statement = impl_->registry_keys;
    sqlite3_reset(statement.handle);
    sqlite3_bind_int(statement.handle, 1, PhaseValue(impl_->phase));
    BindText(statement.handle, 2, snapshot::RegistryKeyLookupKey(path));
    BindText(statement.handle, 3, path);

    return StepDone(impl_->database.handle, statement, "failed to write a registry key", error);
}

bool SnapshotWriter::WriteRegistryValue(const SnapshotRegistryValueEntry& entry, std::string& error)
{
    if (!impl_->in_phase)
    {
        error = "no phase of the snapshot is being written";
        return false;
    }

    auto& statement = impl_->registry_values;
    sqlite3_reset(statement.handle);
    sqlite3_bind_int(statement.handle, 1, PhaseValue(impl_->phase));
    BindText(statement.handle, 2, snapshot::RegistryValueLookupKey(entry.path, entry.name));
    BindText(statement.handle, 3, entry.path);
    BindText(statement.handle, 4, entry.name);
    sqlite3_bind_int64(statement.handle, 5, static_cast<sqlite3_int64>(entry.type));
    BindBlob(statement.handle, 6, entry.data);

    return StepDone(impl_->database.handle, statement, "failed to write a registry value", error);
}

bool SnapshotWriter::WriteEnvironment(const SnapshotEnvironmentEntry& entry, std::string& error)
{
    if (!impl_->in_phase)
    {
        error = "no phase of the snapshot is being written";
        return false;
    }

    auto& statement = impl_->environment;
    sqlite3_reset(statement.handle);
    sqlite3_bind_int(statement.handle, 1, PhaseValue(impl_->phase));
    BindText(statement.handle, 2, snapshot::EnvironmentLookupKey(entry.name));
    BindText(statement.handle, 3, entry.name);
    BindText(statement.handle, 4, entry.value);

    return StepDone(impl_->database.handle, statement, "failed to write an environment variable", error);
}

bool SnapshotWriter::CommitPhase(std::string& error)
{
    if (!impl_->in_phase)
    {
        error = "no phase of the snapshot is being written";
        return false;
    }

    /*
     * The marker is what tells a state which was read as a whole from the rows
     * of a run which was cancelled, so it is written before the transaction is
     * committed: a phase whose commit reports success is a state a comparison
     * accepts.
     */
    auto& statement = impl_->meta;
    sqlite3_reset(statement.handle);
    BindText(statement.handle, 1, std::string(PhaseMetaKey(impl_->phase)));
    BindText(statement.handle, 2, std::to_string(std::time(nullptr)));
    if (!StepDone(impl_->database.handle, statement, "failed to mark the phase of the snapshot", error))
    {
        return false;
    }

    if (!Execute(impl_->database.handle, "COMMIT", error))
    {
        return false;
    }

    impl_->in_phase = false;
    return true;
}

void SnapshotWriter::AbandonPhase()
{
    if (!impl_->in_phase)
    {
        return;
    }

    /*
     * The journal of the database is turned off, which leaves the database
     * without a usable rollback, so the transaction is closed with a commit.
     * The phase carries no marker, so no reader accepts the rows it holds and
     * the next run which writes the phase clears them.
     */
    std::string ignored;
    Execute(impl_->database.handle, "COMMIT", ignored);
    impl_->in_phase = false;
}

bool SnapshotWriter::DiscardFile()
{
    AbandonPhase();

    /* The statements go first: a database which still holds one cannot be closed. */
    impl_->FinalizeStatements();
    impl_->database.Close();

    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(impl_->path), ec);
    return !ec;
}

const std::wstring& SnapshotWriter::Path() const
{
    return impl_->path;
}

SnapshotReader::SnapshotReader() : impl_(new Impl())
{
}

SnapshotReader::~SnapshotReader() = default;

std::unique_ptr<SnapshotReader> SnapshotReader::Open(const std::wstring& path, std::string& error)
{
    if (path.empty())
    {
        error = "no snapshot file path was given";
        return nullptr;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::path(path), ec) || ec)
    {
        error = "cannot open the snapshot file: " + WideToUTF8(path);
        return nullptr;
    }

    auto reader = std::unique_ptr<SnapshotReader>(new SnapshotReader());
    reader->impl_->path = path;

    if (!OpenDatabase(path, false, reader->impl_->database, error))
    {
        return nullptr;
    }

    return reader;
}

bool SnapshotReader::HasPhase(SnapshotPhase phase, bool& present, std::string& error) const
{
    std::string value;
    bool        found = false;
    if (!ReadMeta(impl_->database.handle, PhaseMetaKey(phase), "failed to read the state of the snapshot file", value,
                  found, error))
    {
        return false;
    }

    present = found;
    return true;
}

bool SnapshotReader::Counts(SnapshotPhase phase, SnapshotPhaseCounts& counts, std::string& error) const
{
    SnapshotPhaseCounts result;
    if (!CountRows(impl_->database.handle, "filesystem", phase, result.files, error) ||
        !CountRows(impl_->database.handle, "registry_keys", phase, result.registry_keys, error) ||
        !CountRows(impl_->database.handle, "registry_values", phase, result.registry_values, error) ||
        !CountRows(impl_->database.handle, "environment", phase, result.environment, error))
    {
        return false;
    }

    counts = result;
    return true;
}

bool SnapshotReader::ForEachFile(SnapshotPhase phase, const std::function<bool(const SnapshotFileEntry&)>& visit,
                                 std::string& error)
{
    return ForEachEntry<SnapshotFileEntry>(impl_->database.handle, SelectSql("filesystem", kFileColumns, false),
                                           "failed to read a filesystem entry of the snapshot", phase, DecodeFile,
                                           visit, error);
}

bool SnapshotReader::FindFile(SnapshotPhase phase, const std::string& preset_id, const std::wstring& path,
                              SnapshotFileEntry& entry, bool& found, std::string& error) const
{
    return FindEntry<SnapshotFileEntry>(impl_->database.handle, SelectSql("filesystem", kFileColumns, true),
                                        "failed to look up a filesystem entry of the snapshot", phase,
                                        snapshot::FileLookupKey(preset_id, path), DecodeFile, entry, found, error);
}

bool SnapshotReader::ForEachRegistryKey(SnapshotPhase phase, const std::function<bool(const std::wstring&)>& visit,
                                        std::string& error)
{
    return ForEachEntry<std::wstring>(impl_->database.handle, SelectSql("registry_keys", kRegistryKeyColumns, false),
                                      "failed to read a registry key of the snapshot", phase, DecodeRegistryKey, visit,
                                      error);
}

bool SnapshotReader::FindRegistryKey(SnapshotPhase phase, const std::wstring& path, std::wstring& found_path,
                                     bool& found, std::string& error) const
{
    return FindEntry<std::wstring>(impl_->database.handle, SelectSql("registry_keys", kRegistryKeyColumns, true),
                                   "failed to look up a registry key of the snapshot", phase,
                                   snapshot::RegistryKeyLookupKey(path), DecodeRegistryKey, found_path, found, error);
}

bool SnapshotReader::ForEachRegistryValue(SnapshotPhase                                                 phase,
                                          const std::function<bool(const SnapshotRegistryValueEntry&)>& visit,
                                          std::string&                                                  error)
{
    return ForEachEntry<SnapshotRegistryValueEntry>(
        impl_->database.handle, SelectSql("registry_values", kRegistryValueColumns, false),
        "failed to read a registry value of the snapshot", phase, DecodeRegistryValue, visit, error);
}

bool SnapshotReader::FindRegistryValue(SnapshotPhase phase, const std::wstring& path, const std::wstring& name,
                                       SnapshotRegistryValueEntry& entry, bool& found, std::string& error) const
{
    return FindEntry<SnapshotRegistryValueEntry>(
        impl_->database.handle, SelectSql("registry_values", kRegistryValueColumns, true),
        "failed to look up a registry value of the snapshot", phase, snapshot::RegistryValueLookupKey(path, name),
        DecodeRegistryValue, entry, found, error);
}

bool SnapshotReader::ForEachEnvironment(SnapshotPhase                                               phase,
                                        const std::function<bool(const SnapshotEnvironmentEntry&)>& visit,
                                        std::string&                                                error)
{
    return ForEachEntry<SnapshotEnvironmentEntry>(
        impl_->database.handle, SelectSql("environment", kEnvironmentColumns, false),
        "failed to read an environment variable of the snapshot", phase, DecodeEnvironment, visit, error);
}

bool SnapshotReader::FindEnvironment(SnapshotPhase phase, const std::wstring& name, SnapshotEnvironmentEntry& entry,
                                     bool& found, std::string& error) const
{
    return FindEntry<SnapshotEnvironmentEntry>(
        impl_->database.handle, SelectSql("environment", kEnvironmentColumns, true),
        "failed to look up an environment variable of the snapshot", phase, snapshot::EnvironmentLookupKey(name),
        DecodeEnvironment, entry, found, error);
}

bool CopySnapshotPhase(const std::wstring& source, SnapshotPhase phase, const std::wstring& destination,
                       std::string& error)
{
    const auto reader = SnapshotReader::Open(source, error);
    if (reader == nullptr)
    {
        return false;
    }

    bool present = false;
    if (!reader->HasPhase(phase, present, error))
    {
        return false;
    }
    if (!present)
    {
        error = std::string("the snapshot file does not hold ") + PhaseText(phase) + ": " + WideToUTF8(source);
        return false;
    }

    const auto writer = SnapshotWriter::Create(destination, error);
    if (writer == nullptr)
    {
        return false;
    }

    if (!writer->BeginPhase(phase, error))
    {
        writer->DiscardFile();
        return false;
    }

    /*
     * The error of a write is reported by the writer, so the walk of a part
     * carries its own description: the callback stops the walk and the caller
     * reports the reason.
     */
    std::string write_error;
    bool        copied = reader->ForEachFile(
        phase,
        [&writer, &write_error](const SnapshotFileEntry& entry) { return writer->WriteFile(entry, write_error); },
        error);
    if (copied && write_error.empty())
    {
        copied = reader->ForEachRegistryKey(
            phase,
            [&writer, &write_error](const std::wstring& key) { return writer->WriteRegistryKey(key, write_error); },
            error);
    }
    if (copied && write_error.empty())
    {
        copied = reader->ForEachRegistryValue(
            phase,
            [&writer, &write_error](const SnapshotRegistryValueEntry& entry) {
                return writer->WriteRegistryValue(entry, write_error);
            },
            error);
    }
    if (copied && write_error.empty())
    {
        copied = reader->ForEachEnvironment(
            phase,
            [&writer, &write_error](const SnapshotEnvironmentEntry& entry) {
                return writer->WriteEnvironment(entry, write_error);
            },
            error);
    }

    if (!copied)
    {
        writer->DiscardFile();
        return false;
    }
    if (!write_error.empty())
    {
        error = write_error;
        writer->DiscardFile();
        return false;
    }

    if (!writer->CommitPhase(error))
    {
        writer->DiscardFile();
        return false;
    }

    return true;
}

} // namespace appbox
