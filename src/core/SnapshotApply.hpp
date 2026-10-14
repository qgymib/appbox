#ifndef APPBOX_PACKER_CORE_SNAPSHOT_APPLY_HPP
#define APPBOX_PACKER_CORE_SNAPSHOT_APPLY_HPP

#include "EnvironmentModel.hpp"
#include "PackModel.hpp"
#include "RegistryModel.hpp"
#include "SnapshotDiff.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief What a difference of two snapshots did to the models of a session.
 *
 * The report counts the entries which were written into the models and lists
 * the entries which could not be expressed, so the user sees what the capture
 * did and what it had to leave out. The list of the skipped entries is capped,
 * because a capture of a whole machine can report thousands of them; the count
 * of the entries which were left out is part of the last message.
 */
struct SnapshotApplyReport
{
    /**
     * @brief Number of folders which were imported below a preset directory.
     */
    std::size_t imported_folders = 0;

    /**
     * @brief Number of entries the archive hides.
     */
    std::size_t whiteouts = 0;

    /**
     * @brief Number of registry keys which were created.
     */
    std::size_t registry_keys = 0;

    /**
     * @brief Number of registry values which were written.
     */
    std::size_t registry_values = 0;

    /**
     * @brief Number of registry entries which were hidden.
     *
     * The count covers the removed keys and the removed values: both are
     * expressed with the `Hide` mode of the entry, which keeps the entry
     * invisible for the packaged application.
     */
    std::size_t hidden_registry_entries = 0;

    /**
     * @brief Number of environment variables which were written.
     */
    std::size_t environment_entries = 0;

    /**
     * @brief The entries which could not be expressed, one message per group.
     */
    std::vector<std::wstring> skipped;
};

/**
 * @brief Write the difference of two snapshots into the models of a session.
 *
 * The call is the second half of the capture: the difference is applied to the
 * models of the filesystem, of the registry and of the environment, so the
 * session describes the state the newer snapshot was taken from.
 *
 * The rules are:
 *
 * - A folder which is new below a preset directory is imported, and every
 *   entry below it travels with the import. An entry whose folder existed
 *   before the capture cannot be imported on its own, because the archive
 *   carries whole folders and no single files of a folder it does not hold.
 * - An entry the newer snapshot does not hold anymore is hidden: the archive
 *   carries the whiteout marker of the entry, which hides the entry of the
 *   host while the packaged application runs.
 * - A registry key which is new is created, and a value which is new or whose
 *   data changed is written with the data of the newer snapshot. A key or a
 *   value which is gone is hidden with the `Hide` mode of the entry.
 * - An environment variable which is new or whose value changed is written
 *   with the mode the model uses by default: `Write Copy` and `Replace`, so
 *   the application reads the value of the row. A variable which is gone
 *   cannot be expressed, because a row of the workspace can only report a
 *   value and never the absence of a variable.
 * - The environment keys of the machine are left out of the registry part:
 *   the change they carry is expressed by the environment workspace, which is
 *   what keeps the two domains from reporting the same variable twice.
 *
 * The absolute paths of a registry value and of an environment variable are
 * rewritten to the reference of the preset directory they point into, so the
 * archive stays correct on a machine whose folders are somewhere else.
 *
 * @param[in] diff The difference to apply.
 * @param[in,out] model Model which receives the imported folders and the
 *                hidden entries.
 * @param[in,out] registry Registry which receives the keys and the values.
 * @param[in,out] environment Environment which receives the variables.
 * @param[out] report What the call did and what it left out.
 * @param[out] error Error description when the difference itself is not usable.
 * @return true when the difference was applied; false when it holds an entry
 *         which cannot be interpreted, which leaves the models untouched.
 */
bool ApplySnapshotDiff(const SnapshotDiff& diff, PackModel& model, RegistryModel& registry,
                       EnvironmentModel& environment, SnapshotApplyReport& report, std::string& error);

/**
 * @brief Build the text which summarizes a difference and what a session did with it.
 *
 * The text names the entries the difference holds, the entries which were
 * written into the workspaces and the entries which were left out, so the user
 * sees what a capture did without reading the three workspaces. It is built by
 * the run itself, which keeps the counts of the difference from travelling
 * through the event which reports the result.
 *
 * @param[in] diff The difference of the two snapshots.
 * @param[in] report What the difference did to the models.
 * @return The summary text.
 */
std::wstring SnapshotApplySummary(const SnapshotDiff& diff, const SnapshotApplyReport& report);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_SNAPSHOT_APPLY_HPP
