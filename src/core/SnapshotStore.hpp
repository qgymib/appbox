#ifndef APPBOX_PACKER_CORE_SNAPSHOT_STORE_HPP
#define APPBOX_PACKER_CORE_SNAPSHOT_STORE_HPP

#include "SnapshotModel.hpp"
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace appbox
{

/**
 * @brief Version of the schema of a snapshot file.
 *
 * The version is stored inside the file and is required to match exactly while
 * the file is read, so a file of a future format is rejected instead of being
 * interpreted with the rules of the current one.
 */
inline constexpr int kSnapshotSchemaVersion = 2;

/**
 * @brief Extension of a snapshot file, without the leading dot.
 */
inline constexpr const char* kSnapshotFileExtension = "db";

/**
 * @brief One of the two states a snapshot file describes.
 *
 * A snapshot file holds the state of the machine before a change and the state
 * after it: the two are the sides of the comparison the snapshot capture of the
 * packer performs. A phase counts as a state only once it was written as a
 * whole, which is what the marker of the phase records (see
 * `SnapshotWriter::CommitPhase`).
 */
enum class SnapshotPhase
{
    Before, ///< The state of the machine before the change, which is the baseline of a comparison.
    After   ///< The state of the machine after the change.
};

/**
 * @brief Number of the entries of one phase of a snapshot file.
 */
struct SnapshotPhaseCounts
{
    /**
     * @brief Number of the entries of the filesystem part.
     */
    std::size_t files = 0;

    /**
     * @brief Number of the registry keys.
     */
    std::size_t registry_keys = 0;

    /**
     * @brief Number of the registry values.
     */
    std::size_t registry_values = 0;

    /**
     * @brief Number of the environment variables.
     */
    std::size_t environment = 0;
};

/**
 * @brief Writes one phase of a snapshot file entry by entry.
 *
 * The writer is what keeps a capture from holding the state of a whole machine
 * in memory: every entry the walk of `src/core/SnapshotCapture.*` reads is
 * handed to the writer and travels into the file right away, so the memory of
 * a run is the size of one entry and of the page cache of the database
 * instead of the size of the machine.
 *
 * A phase is written inside one transaction and is finished by
 * `CommitPhase()`, which writes the marker of the phase. A phase which was
 * started but never committed holds rows which no reader accepts as a state
 * (`SnapshotReader::HasPhase()`), so a run which the user cancelled leaves the
 * file usable: the state before the change stays readable and the next run of
 * the same phase clears the rows which were left behind.
 *
 * The database is written with its journal turned off, which is the trade a
 * snapshot makes to stay in the range of seconds for a whole machine. That
 * mode has no usable rollback, so the writer never rolls a phase back: a
 * cancelled phase is abandoned (see `AbandonPhase()`), which is what keeps the
 * other phase of the file in place.
 */
class SnapshotWriter
{
public:
    /**
     * @brief Create a snapshot file, replacing a file which exists already.
     *
     * The file is removed first, because SQLite would otherwise open a file
     * which carries the schema it already holds and refuse to create the
     * tables a second time.
     *
     * @param[in] path Destination path of the snapshot file.
     * @param[out] error Error description on failure.
     * @return The writer, null when the file could not be created.
     */
    static std::unique_ptr<SnapshotWriter> Create(const std::wstring& path, std::string& error);

    /**
     * @brief Open a snapshot file which exists already.
     *
     * The file has to be a snapshot of this build: a file which carries no
     * schema version, or a version this build does not know, is refused.
     *
     * @param[in] path Path of the snapshot file.
     * @param[out] error Error description on failure.
     * @return The writer, null when the file could not be opened.
     */
    static std::unique_ptr<SnapshotWriter> Open(const std::wstring& path, std::string& error);

    /**
     * @brief Abandon a phase which was not committed and release the file.
     */
    ~SnapshotWriter();

    SnapshotWriter(const SnapshotWriter&) = delete;
    SnapshotWriter& operator=(const SnapshotWriter&) = delete;

    /**
     * @brief Start writing one phase.
     *
     * The entries of the phase are removed first, so a phase which is written
     * a second time holds the state of the newer run alone. Writing the state
     * before the change clears both phases: a baseline which replaces the
     * baseline of the file must not leave the state of an earlier comparison
     * behind.
     *
     * @param[in] phase Phase to write.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool BeginPhase(SnapshotPhase phase, std::string& error);

    /**
     * @brief Write one entry of the filesystem part.
     * @param[in] entry The entry to write.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool WriteFile(const SnapshotFileEntry& entry, std::string& error);

    /**
     * @brief Write one key of the registry part.
     * @param[in] path Path of the key.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool WriteRegistryKey(const std::wstring& path, std::string& error);

    /**
     * @brief Write one value of the registry part.
     * @param[in] entry The value to write.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool WriteRegistryValue(const SnapshotRegistryValueEntry& entry, std::string& error);

    /**
     * @brief Write one variable of the environment part.
     *
     * A name which the phase holds already replaces the entry which is stored,
     * which is what expresses the precedence the persisted environment of the
     * machine applies: the variables of the current user win over the
     * variables of the machine, so the run which reads the user key writes
     * last.
     *
     * @param[in] entry The variable to write.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool WriteEnvironment(const SnapshotEnvironmentEntry& entry, std::string& error);

    /**
     * @brief Finish the phase and mark it as a state which was read as a whole.
     *
     * The marker is written before the transaction is committed, so a phase
     * which reports a successful commit is a state a comparison accepts.
     *
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool CommitPhase(std::string& error);

    /**
     * @brief End a phase which was not committed without marking it.
     *
     * The rows which were written stay in the file and are cleared by the next
     * run which writes the same phase; a reader never accepts them as a state,
     * because the marker of the phase is missing. The call is what a cancelled
     * capture does: it keeps the other phase of the file in place, which is
     * the baseline of the comparison.
     */
    void AbandonPhase();

    /**
     * @brief Close the file and remove it.
     *
     * The call is what a failed or cancelled capture of the state before a
     * change does: a half written baseline is of no use, so the caller never
     * sees it.
     *
     * @return true when the file is gone.
     */
    bool DiscardFile();

    /**
     * @brief Get the path of the snapshot file.
     * @return The path.
     */
    const std::wstring& Path() const;

private:
    SnapshotWriter();

    /**
     * @brief Compile the statements the writer writes its entries with.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool PrepareStatements(std::string& error);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Reads one phase of a snapshot file entry by entry.
 *
 * The reader is the counterpart of the writer: it walks the entries of a phase
 * in the stable order of their identifier and it looks up one entry by the
 * name of the host, so a comparison of two phases needs neither the whole
 * state nor a copy of it in memory.
 *
 * Every method which walks a part of a phase hands one entry at a time to a
 * callback. A callback which returns false stops the walk, which is how a
 * caller aborts it on an error of its own; the walk reports a failure of the
 * database through the error description instead.
 */
class SnapshotReader
{
public:
    /**
     * @brief Open a snapshot file.
     *
     * The file has to be a snapshot of this build: a file which carries no
     * schema version, or a version this build does not know, is refused. The
     * call never creates a file.
     *
     * @param[in] path Path of the snapshot file.
     * @param[out] error Error description on failure.
     * @return The reader, null when the file could not be opened.
     */
    static std::unique_ptr<SnapshotReader> Open(const std::wstring& path, std::string& error);

    ~SnapshotReader();

    SnapshotReader(const SnapshotReader&) = delete;
    SnapshotReader& operator=(const SnapshotReader&) = delete;

    /**
     * @brief Whether the file holds the phase as a whole.
     * @param[in] phase Phase to test.
     * @param[out] present Whether the phase was written as a whole.
     * @param[out] error Error description on failure.
     * @return true when the question could be answered.
     */
    bool HasPhase(SnapshotPhase phase, bool& present, std::string& error) const;

    /**
     * @brief Count the entries of one phase.
     * @param[in] phase Phase to count.
     * @param[out] counts The counts of the phase.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool Counts(SnapshotPhase phase, SnapshotPhaseCounts& counts, std::string& error) const;

    /**
     * @brief Visit every entry of the filesystem part of a phase.
     * @param[in] phase Phase to read.
     * @param[in] visit Callback invoked per entry; false stops the walk.
     * @param[out] error Error description when the query fails.
     * @return false when the query failed.
     */
    bool ForEachFile(SnapshotPhase phase, const std::function<bool(const SnapshotFileEntry&)>& visit,
                     std::string& error);

    /**
     * @brief Look up one entry of the filesystem part of a phase.
     * @param[in] phase Phase to read.
     * @param[in] preset_id Identifier of the preset directory of the entry.
     * @param[in] path Path below the preset directory, any case.
     * @param[out] entry The entry which was found.
     * @param[out] found Whether the phase holds the entry.
     * @param[out] error Error description on failure.
     * @return true when the lookup could be answered.
     */
    bool FindFile(SnapshotPhase phase, const std::string& preset_id, const std::wstring& path, SnapshotFileEntry& entry,
                  bool& found, std::string& error) const;

    /**
     * @brief Visit every key of the registry part of a phase.
     * @param[in] phase Phase to read.
     * @param[in] visit Callback invoked per key; false stops the walk.
     * @param[out] error Error description when the query fails.
     * @return false when the query failed.
     */
    bool ForEachRegistryKey(SnapshotPhase phase, const std::function<bool(const std::wstring&)>& visit,
                            std::string& error);

    /**
     * @brief Look up one key of the registry part of a phase.
     * @param[in] phase Phase to read.
     * @param[in] path Path of the key, any case.
     * @param[out] found_path Path of the key as the phase spells it.
     * @param[out] found Whether the phase holds the key.
     * @param[out] error Error description on failure.
     * @return true when the lookup could be answered.
     */
    bool FindRegistryKey(SnapshotPhase phase, const std::wstring& path, std::wstring& found_path, bool& found,
                         std::string& error) const;

    /**
     * @brief Visit every value of the registry part of a phase.
     * @param[in] phase Phase to read.
     * @param[in] visit Callback invoked per value; false stops the walk.
     * @param[out] error Error description when the query fails.
     * @return false when the query failed.
     */
    bool ForEachRegistryValue(SnapshotPhase phase, const std::function<bool(const SnapshotRegistryValueEntry&)>& visit,
                              std::string& error);

    /**
     * @brief Look up one value of the registry part of a phase.
     * @param[in] phase Phase to read.
     * @param[in] path Path of the key which holds the value, any case.
     * @param[in] name Name of the value, any case, empty for the default value.
     * @param[out] entry The value which was found.
     * @param[out] found Whether the phase holds the value.
     * @param[out] error Error description on failure.
     * @return true when the lookup could be answered.
     */
    bool FindRegistryValue(SnapshotPhase phase, const std::wstring& path, const std::wstring& name,
                           SnapshotRegistryValueEntry& entry, bool& found, std::string& error) const;

    /**
     * @brief Visit every variable of the environment part of a phase.
     * @param[in] phase Phase to read.
     * @param[in] visit Callback invoked per variable; false stops the walk.
     * @param[out] error Error description when the query fails.
     * @return false when the query failed.
     */
    bool ForEachEnvironment(SnapshotPhase phase, const std::function<bool(const SnapshotEnvironmentEntry&)>& visit,
                            std::string& error);

    /**
     * @brief Look up one variable of the environment part of a phase.
     * @param[in] phase Phase to read.
     * @param[in] name Name of the variable, any case.
     * @param[out] entry The variable which was found.
     * @param[out] found Whether the phase holds the variable.
     * @param[out] error Error description on failure.
     * @return true when the lookup could be answered.
     */
    bool FindEnvironment(SnapshotPhase phase, const std::wstring& name, SnapshotEnvironmentEntry& entry, bool& found,
                         std::string& error) const;

private:
    SnapshotReader();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Copy one phase of a snapshot file into a file of its own.
 *
 * The copy holds the phase as the baseline of the file, so it can be loaded as
 * the baseline of a comparison later on. The call is what the `Save Snapshot
 * As...` command of the packer does: it writes the state before the change
 * into a file the user picks and leaves the file the session works with alone.
 *
 * The destination is created or replaced, and a copy which fails leaves no
 * file behind.
 *
 * @param[in] source Path of the snapshot file to read.
 * @param[in] phase Phase to copy.
 * @param[in] destination Destination path of the copy.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool CopySnapshotPhase(const std::wstring& source, SnapshotPhase phase, const std::wstring& destination,
                       std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_SNAPSHOT_STORE_HPP
