#include "SnapshotDiff.hpp"
#include "SnapshotStore.hpp"
#include "WString.hpp"
#include <string>
#include <utility>

namespace
{

using appbox::SnapshotDiff;
using appbox::SnapshotDiffEnvironment;
using appbox::SnapshotDiffFile;
using appbox::SnapshotDiffRegistryValue;
using appbox::SnapshotEnvironmentEntry;
using appbox::SnapshotFileEntry;
using appbox::SnapshotPhase;
using appbox::SnapshotReader;
using appbox::SnapshotRegistryValueEntry;

/**
 * @brief Report the entries which only the newer state of the filesystem holds.
 *
 * The walk of the newer state looks up every entry in the older one, so an
 * entry the older state does not hold was added by the change and an entry
 * both states hold with another size or write time was modified by it.
 *
 * @param[in] reader The snapshot file to read.
 * @param[out] diff The difference to fill.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool CollectAddedFiles(SnapshotReader& reader, SnapshotDiff& diff, std::string& error)
{
    const auto walked = reader.ForEachFile(
        SnapshotPhase::After,
        [&reader, &diff, &error](const SnapshotFileEntry& entry) {
            SnapshotFileEntry earlier;
            bool              found = false;
            if (!reader.FindFile(SnapshotPhase::Before, entry.preset_id, entry.path, earlier, found, error))
            {
                return false;
            }

            if (!found)
            {
                SnapshotDiffFile added;
                added.preset_id = entry.preset_id;
                added.path = entry.path;
                added.directory = entry.directory;
                diff.added_files.push_back(std::move(added));
                return true;
            }

            /*
             * A directory carries neither a size nor a write time of its own,
             * so it is only ever added or removed.
             */
            const bool changed = !entry.directory && !earlier.directory &&
                                 (entry.size != earlier.size || entry.write_time != earlier.write_time);
            if (changed)
            {
                SnapshotDiffFile modified;
                modified.preset_id = entry.preset_id;
                modified.path = entry.path;
                modified.directory = false;
                diff.modified_files.push_back(std::move(modified));
            }

            return true;
        },
        error);

    return walked && error.empty();
}

/**
 * @brief Report the entries which only the older state of the filesystem holds.
 * @param[in] reader The snapshot file to read.
 * @param[out] diff The difference to fill.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool CollectRemovedFiles(SnapshotReader& reader, SnapshotDiff& diff, std::string& error)
{
    const auto walked = reader.ForEachFile(
        SnapshotPhase::Before,
        [&reader, &diff, &error](const SnapshotFileEntry& entry) {
            SnapshotFileEntry current;
            bool              found = false;
            if (!reader.FindFile(SnapshotPhase::After, entry.preset_id, entry.path, current, found, error))
            {
                return false;
            }

            if (!found)
            {
                SnapshotDiffFile removed;
                removed.preset_id = entry.preset_id;
                removed.path = entry.path;
                removed.directory = entry.directory;
                diff.removed_files.push_back(std::move(removed));
            }

            return true;
        },
        error);

    return walked && error.empty();
}

/**
 * @brief Report the registry keys which only the newer state holds.
 * @param[in] reader The snapshot file to read.
 * @param[out] diff The difference to fill.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool CollectAddedRegistryKeys(SnapshotReader& reader, SnapshotDiff& diff, std::string& error)
{
    const auto walked = reader.ForEachRegistryKey(
        SnapshotPhase::After,
        [&reader, &diff, &error](const std::wstring& path) {
            std::wstring earlier;
            bool         found = false;
            if (!reader.FindRegistryKey(SnapshotPhase::Before, path, earlier, found, error))
            {
                return false;
            }

            if (!found)
            {
                diff.added_registry_keys.push_back(path);
            }

            return true;
        },
        error);

    return walked && error.empty();
}

/**
 * @brief Report the registry keys which only the older state holds.
 * @param[in] reader The snapshot file to read.
 * @param[out] diff The difference to fill.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool CollectRemovedRegistryKeys(SnapshotReader& reader, SnapshotDiff& diff, std::string& error)
{
    const auto walked = reader.ForEachRegistryKey(
        SnapshotPhase::Before,
        [&reader, &diff, &error](const std::wstring& path) {
            std::wstring current;
            bool         found = false;
            if (!reader.FindRegistryKey(SnapshotPhase::After, path, current, found, error))
            {
                return false;
            }

            if (!found)
            {
                diff.removed_registry_keys.push_back(path);
            }

            return true;
        },
        error);

    return walked && error.empty();
}

/**
 * @brief Report the registry values which the newer state added or changed.
 * @param[in] reader The snapshot file to read.
 * @param[out] diff The difference to fill.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool CollectAddedRegistryValues(SnapshotReader& reader, SnapshotDiff& diff, std::string& error)
{
    const auto walked = reader.ForEachRegistryValue(
        SnapshotPhase::After,
        [&reader, &diff, &error](const SnapshotRegistryValueEntry& value) {
            SnapshotRegistryValueEntry earlier;
            bool                       found = false;
            if (!reader.FindRegistryValue(SnapshotPhase::Before, value.path, value.name, earlier, found, error))
            {
                return false;
            }

            if (found && earlier.type == value.type && earlier.data == value.data)
            {
                return true;
            }

            SnapshotDiffRegistryValue entry;
            entry.path = value.path;
            entry.name = value.name;
            entry.type = value.type;
            entry.data = value.data;
            if (found)
            {
                diff.modified_registry_values.push_back(std::move(entry));
            }
            else
            {
                diff.added_registry_values.push_back(std::move(entry));
            }

            return true;
        },
        error);

    return walked && error.empty();
}

/**
 * @brief Report the registry values which only the older state holds.
 * @param[in] reader The snapshot file to read.
 * @param[out] diff The difference to fill.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool CollectRemovedRegistryValues(SnapshotReader& reader, SnapshotDiff& diff, std::string& error)
{
    const auto walked = reader.ForEachRegistryValue(
        SnapshotPhase::Before,
        [&reader, &diff, &error](const SnapshotRegistryValueEntry& value) {
            SnapshotRegistryValueEntry current;
            bool                       found = false;
            if (!reader.FindRegistryValue(SnapshotPhase::After, value.path, value.name, current, found, error))
            {
                return false;
            }

            if (!found)
            {
                SnapshotDiffRegistryValue entry;
                entry.path = value.path;
                entry.name = value.name;
                entry.type = value.type;
                entry.data = value.data;
                diff.removed_registry_values.push_back(std::move(entry));
            }

            return true;
        },
        error);

    return walked && error.empty();
}

/**
 * @brief Report the variables which the newer state added or changed.
 * @param[in] reader The snapshot file to read.
 * @param[out] diff The difference to fill.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool CollectAddedEnvironment(SnapshotReader& reader, SnapshotDiff& diff, std::string& error)
{
    const auto walked = reader.ForEachEnvironment(
        SnapshotPhase::After,
        [&reader, &diff, &error](const SnapshotEnvironmentEntry& entry) {
            SnapshotEnvironmentEntry earlier;
            bool                     found = false;
            if (!reader.FindEnvironment(SnapshotPhase::Before, entry.name, earlier, found, error))
            {
                return false;
            }

            if (found && earlier.value == entry.value)
            {
                return true;
            }

            SnapshotDiffEnvironment variable;
            variable.name = entry.name;
            variable.value = entry.value;
            if (found)
            {
                diff.modified_environment.push_back(std::move(variable));
            }
            else
            {
                diff.added_environment.push_back(std::move(variable));
            }

            return true;
        },
        error);

    return walked && error.empty();
}

/**
 * @brief Report the variables which only the older state holds.
 * @param[in] reader The snapshot file to read.
 * @param[out] diff The difference to fill.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool CollectRemovedEnvironment(SnapshotReader& reader, SnapshotDiff& diff, std::string& error)
{
    const auto walked = reader.ForEachEnvironment(
        SnapshotPhase::Before,
        [&reader, &diff, &error](const SnapshotEnvironmentEntry& entry) {
            SnapshotEnvironmentEntry current;
            bool                     found = false;
            if (!reader.FindEnvironment(SnapshotPhase::After, entry.name, current, found, error))
            {
                return false;
            }

            if (!found)
            {
                SnapshotDiffEnvironment variable;
                variable.name = entry.name;
                variable.value = entry.value;
                diff.removed_environment.push_back(std::move(variable));
            }

            return true;
        },
        error);

    return walked && error.empty();
}

/**
 * @brief Describe one phase inside an error message.
 * @param[in] phase The phase.
 * @return The description of the phase.
 */
const char* PhaseText(SnapshotPhase phase)
{
    return phase == SnapshotPhase::Before ? "the state before the change" : "the state after the change";
}

} // namespace

namespace appbox
{

bool SnapshotDiff::IsEmpty() const
{
    return added_files.empty() && modified_files.empty() && removed_files.empty() && added_registry_keys.empty() &&
           removed_registry_keys.empty() && added_registry_values.empty() && modified_registry_values.empty() &&
           removed_registry_values.empty() && added_environment.empty() && modified_environment.empty() &&
           removed_environment.empty();
}

bool DiffSnapshotPhases(const std::wstring& path, SnapshotDiff& diff, std::string& error)
{
    std::string error_text;
    const auto  reader = SnapshotReader::Open(path, error_text);
    if (reader == nullptr)
    {
        error = error_text;
        return false;
    }

    /*
     * A phase which was never written holds no entry at all, so a comparison
     * against it would report the whole machine as removed: both states have
     * to be there.
     */
    const auto require = [&reader, &path, &error_text](SnapshotPhase phase) {
        bool present = false;
        if (!reader->HasPhase(phase, present, error_text))
        {
            return false;
        }
        if (present)
        {
            return true;
        }

        error_text = std::string("the snapshot file does not hold ") + PhaseText(phase) + ": " + WideToUTF8(path);
        return false;
    };

    if (!require(SnapshotPhase::Before) || !require(SnapshotPhase::After))
    {
        error = error_text;
        return false;
    }

    SnapshotDiff result;
    if (!CollectAddedFiles(*reader, result, error_text) || !CollectRemovedFiles(*reader, result, error_text) ||
        !CollectAddedRegistryKeys(*reader, result, error_text) ||
        !CollectRemovedRegistryKeys(*reader, result, error_text) ||
        !CollectAddedRegistryValues(*reader, result, error_text) ||
        !CollectRemovedRegistryValues(*reader, result, error_text) ||
        !CollectAddedEnvironment(*reader, result, error_text) ||
        !CollectRemovedEnvironment(*reader, result, error_text))
    {
        error = error_text;
        return false;
    }

    diff = std::move(result);
    return true;
}

} // namespace appbox
