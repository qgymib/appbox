#ifndef APPBOX_PACKER_CORE_SNAPSHOT_MODEL_HPP
#define APPBOX_PACKER_CORE_SNAPSHOT_MODEL_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief One entry of the filesystem part of a snapshot.
 *
 * The path is stored relative to the preset directory the entry belongs to
 * (`C:\Program Files\Foo\bar.exe` is the entry `(program_files, "Foo\bar.exe")`),
 * so a snapshot does not depend on the machine it was taken on and a difference
 * of two snapshots can be applied to the pack model without matching host paths
 * again.
 */
struct SnapshotFileEntry
{
    /**
     * @brief Identifier of the preset directory which owns the entry.
     */
    std::string preset_id;

    /**
     * @brief Path below the preset directory, backslash separated.
     */
    std::wstring path;

    /**
     * @brief Whether the entry is a directory.
     */
    bool directory = false;

    /**
     * @brief Size of a file in bytes, zero for a directory.
     */
    std::uint64_t size = 0;

    /**
     * @brief Last write time in 100ns units since 1601-01-01 UTC, zero when
     *        the host does not report one.
     */
    std::int64_t write_time = 0;
};

/**
 * @brief One value of the registry part of a snapshot.
 *
 * The data holds the raw bytes of the value together with its `REG_*` type
 * code, which is the representation the registry API uses, so a value of a
 * captured key travels into the pack model unchanged.
 */
struct SnapshotRegistryValueEntry
{
    /**
     * @brief Path of the key which holds the value, e.g.
     *        `L"HKEY_CURRENT_USER\\Software\\Foo"`.
     */
    std::wstring path;

    /**
     * @brief Name of the value, empty for the default value of the key.
     */
    std::wstring name;

    /**
     * @brief The `REG_*` type code of the value.
     */
    std::uint32_t type = 0;

    /**
     * @brief Raw data of the value.
     */
    std::vector<std::uint8_t> data;
};

/**
 * @brief One environment variable of a snapshot.
 *
 * The value is stored as the persisted environment of the machine spells it
 * with every `%NAME%` reference already resolved, see
 * `src/core/SnapshotCapture.hpp`.
 */
struct SnapshotEnvironmentEntry
{
    /**
     * @brief Name of the variable.
     */
    std::wstring name;

    /**
     * @brief Value of the variable, which may be empty.
     */
    std::wstring value;
};

/**
 * @brief Vocabulary of the snapshot comparison.
 *
 * A difference of two snapshots is computed on the identifiers below: they fold
 * the case of every name, because the filesystem, the registry and the
 * environment of Windows compare their names without regard to the case.
 *
 * The identifier of an entry is also the key a snapshot file stores it under
 * (see `src/core/SnapshotStore.hpp`), so the lookup of one entry of a phase is
 * a key lookup and the entries of a phase are read in a stable order.
 */
namespace snapshot
{

/**
 * @brief Fold the case of a name of the snapshot.
 *
 * The comparison of the entries of two snapshots ignores the case, so both
 * sides are folded into the same spelling first.
 *
 * @param[in] text Text to fold.
 * @return The folded text.
 */
std::wstring FoldText(const std::wstring& text);

/**
 * @brief Build the identifier of one entry of the filesystem part.
 * @param[in] preset_id Identifier of the preset directory.
 * @param[in] path Path below the preset directory.
 * @return The folded identifier of the entry.
 */
std::wstring FileLookupKey(const std::string& preset_id, const std::wstring& path);

/**
 * @brief Build the identifier of one key of the registry part.
 * @param[in] path Path of the key.
 * @return The folded identifier of the key.
 */
std::wstring RegistryKeyLookupKey(const std::wstring& path);

/**
 * @brief Build the identifier of one value of the registry part.
 * @param[in] path Path of the key which holds the value.
 * @param[in] name Name of the value, empty for the default value.
 * @return The folded identifier of the value.
 */
std::wstring RegistryValueLookupKey(const std::wstring& path, const std::wstring& name);

/**
 * @brief Build the identifier of one environment variable.
 * @param[in] name Name of the variable.
 * @return The folded identifier of the variable.
 */
std::wstring EnvironmentLookupKey(const std::wstring& name);

} // namespace snapshot

} // namespace appbox

#endif // APPBOX_PACKER_CORE_SNAPSHOT_MODEL_HPP
