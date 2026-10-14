#ifndef APPBOX_PACKER_CORE_SNAPSHOT_DIFF_HPP
#define APPBOX_PACKER_CORE_SNAPSHOT_DIFF_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief One entry of the filesystem part a difference reports.
 */
struct SnapshotDiffFile
{
    /**
     * @brief Identifier of the preset directory which owns the entry.
     */
    std::string preset_id;

    /**
     * @brief Path below the preset directory.
     */
    std::wstring path;

    /**
     * @brief Whether the entry is a directory.
     */
    bool directory = false;
};

/**
 * @brief One value of the registry part a difference reports.
 */
struct SnapshotDiffRegistryValue
{
    /**
     * @brief Path of the key which holds the value.
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
 * @brief One environment variable a difference reports.
 */
struct SnapshotDiffEnvironment
{
    /**
     * @brief Name of the variable.
     */
    std::wstring name;

    /**
     * @brief Value of the variable.
     */
    std::wstring value;
};

/**
 * @brief The difference of two snapshots.
 *
 * The structure holds one list per kind of change and per part of the machine:
 * the entries which only the newer snapshot holds, the entries which both
 * snapshots hold with another content and the entries which only the older
 * snapshot holds. The names are compared ignoring the case, because the
 * filesystem, the registry and the environment of Windows do.
 *
 * A difference is pure data: it holds no host handle and no wxWidgets
 * dependency, so it can be built by a test and handed to the models of the
 * packer. Its size is the size of the change and not the size of the machine,
 * which is what keeps a comparison of two states of a whole machine out of the
 * memory of the packer.
 */
struct SnapshotDiff
{
    /**
     * @brief The entries the newer snapshot holds and the older one does not.
     */
    std::vector<SnapshotDiffFile> added_files;

    /**
     * @brief The files both snapshots hold with another size or write time.
     */
    std::vector<SnapshotDiffFile> modified_files;

    /**
     * @brief The entries the older snapshot holds and the newer one does not.
     */
    std::vector<SnapshotDiffFile> removed_files;

    /**
     * @brief The registry keys the newer snapshot holds and the older one does not.
     */
    std::vector<std::wstring> added_registry_keys;

    /**
     * @brief The registry keys the older snapshot holds and the newer one does not.
     */
    std::vector<std::wstring> removed_registry_keys;

    /**
     * @brief The registry values the newer snapshot holds and the older one does not.
     */
    std::vector<SnapshotDiffRegistryValue> added_registry_values;

    /**
     * @brief The registry values both snapshots hold with another type or data.
     */
    std::vector<SnapshotDiffRegistryValue> modified_registry_values;

    /**
     * @brief The registry values the older snapshot holds and the newer one does not.
     */
    std::vector<SnapshotDiffRegistryValue> removed_registry_values;

    /**
     * @brief The variables the newer snapshot holds and the older one does not.
     */
    std::vector<SnapshotDiffEnvironment> added_environment;

    /**
     * @brief The variables both snapshots hold with another value.
     */
    std::vector<SnapshotDiffEnvironment> modified_environment;

    /**
     * @brief The variables the older snapshot holds and the newer one does not.
     */
    std::vector<SnapshotDiffEnvironment> removed_environment;

    /**
     * @brief Whether the difference holds no entry at all.
     * @return true when the two snapshots describe the same state.
     */
    bool IsEmpty() const;
};

/**
 * @brief Compare the two states a snapshot file holds.
 *
 * The call is the comparison of the capture: the state before the change is
 * the baseline and the state after it is what the machine reports now.
 *
 * The entries of a phase are read one at a time and the entry which matches is
 * looked up in the other phase, so neither state is held in memory: the memory
 * of a comparison is the difference it reports. The comparison is a pure
 * function of the file: the same pair of states always yields the same
 * difference, whatever order the two runs recorded their entries in.
 *
 * A file which does not hold both states is refused, and the difference of the
 * caller is only replaced once the whole comparison succeeded.
 *
 * A file counts as modified when its size or its write time differs; a
 * directory carries neither, so a directory is only ever added or removed.
 * A registry value counts as modified when its type or one byte of its data
 * differs.
 *
 * @param[in] path Path of the snapshot file to compare.
 * @param[out] diff The difference of the two states of the file.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool DiffSnapshotPhases(const std::wstring& path, SnapshotDiff& diff, std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_SNAPSHOT_DIFF_HPP
