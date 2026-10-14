#include "SnapshotApply.hpp"
#include "KnownFolderReference.hpp"
#include "PresetDirectory.hpp"
#include "SnapshotModel.hpp"
#include "WString.hpp"
#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{

/**
 * @brief Number of the messages the report lists before it summarizes the rest.
 */
constexpr std::size_t kMaxSkippedMessages = 20;

/**
 * @brief Name of the environment key of the current user and of the machine.
 */
constexpr const wchar_t* kEnvironmentKeyName = L"Environment";

/**
 * @brief Path of the environment key of the machine.
 */
constexpr const wchar_t* kMachineEnvironmentKeyPath =
    L"HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";

/**
 * @brief Whether two texts are equal ignoring the case.
 * @param[in] left Left text.
 * @param[in] right Right text.
 * @return true when both texts differ in the case of their characters alone.
 */
bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (std::towlower(static_cast<std::wint_t>(left[index])) !=
            std::towlower(static_cast<std::wint_t>(right[index])))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Whether a registry path is a key below another one.
 * @param[in] path Path to test.
 * @param[in] ancestor Key to test against.
 * @return true when the path is a strict descendant of the ancestor.
 */
bool IsRegistryPathBelow(const std::wstring& path, const std::wstring& ancestor)
{
    if (ancestor.empty() || path.size() <= ancestor.size())
    {
        return false;
    }
    if (!EqualsIgnoreCase(path.substr(0, ancestor.size()), ancestor))
    {
        return false;
    }

    return path[ancestor.size()] == L'\\';
}

/**
 * @brief Whether a registry path names an environment key of the machine.
 *
 * The variables of the machine are part of the registry as well, so a capture
 * reports a change of them twice. The environment workspace is the domain
 * which expresses them, so the registry part leaves the two keys out.
 *
 * @param[in] path Path of the key.
 * @return true when the path names one of the two environment keys.
 */
bool IsEnvironmentKey(const std::wstring& path)
{
    const auto parts = appbox::SplitRegistryPath(path);

    if (parts.size() == 2)
    {
        return EqualsIgnoreCase(parts[0], L"HKEY_CURRENT_USER") && EqualsIgnoreCase(parts[1], kEnvironmentKeyName);
    }

    if (parts.size() == 6)
    {
        return EqualsIgnoreCase(parts[0], L"HKEY_LOCAL_MACHINE") && EqualsIgnoreCase(parts[1], L"SYSTEM") &&
               EqualsIgnoreCase(parts[2], L"CurrentControlSet") && EqualsIgnoreCase(parts[3], L"Control") &&
               EqualsIgnoreCase(parts[4], L"Session Manager") && EqualsIgnoreCase(parts[5], kEnvironmentKeyName);
    }

    return false;
}

/**
 * @brief Normalize a path of a snapshot.
 * @param[in] path Path to normalize.
 * @return The path with backslashes, unchanged when it carries none.
 */
std::wstring NormalizedPath(const std::wstring& path)
{
    std::wstring result = path;
    for (auto& character : result)
    {
        if (character == L'/')
        {
            character = L'\\';
        }
    }
    return result;
}

/**
 * @brief Whether a path of a snapshot can be interpreted.
 *
 * A path has to name an entry below a preset directory, so an empty path, a
 * path which starts with a separator, a path which carries a drive and a path
 * which leaves its preset directory are refused.
 *
 * @param[in] path Path of an entry of the difference.
 * @return true when the path is usable.
 */
bool IsUsablePath(const std::wstring& path)
{
    if (path.empty())
    {
        return false;
    }
    if (path.front() == L'\\' || path.front() == L'/')
    {
        return false;
    }
    if (path.find(L':') != std::wstring::npos)
    {
        return false;
    }

    for (const auto& part : appbox::Split(NormalizedPath(path), L"\\"))
    {
        if (part == L"..")
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Get the first component of a path below a preset directory.
 * @param[in] path Path of an entry.
 * @return The first component, empty when the path holds none.
 */
std::wstring FirstComponent(const std::wstring& path)
{
    const auto parts = appbox::Split(NormalizedPath(path), L"\\");
    return parts.empty() ? std::wstring() : parts.front();
}

/**
 * @brief Get the directory a path below a preset directory lives in.
 * @param[in] path Path of an entry.
 * @return The parent path, empty for an entry of the layer root.
 */
std::wstring ParentOf(const std::wstring& path)
{
    const auto normalized = NormalizedPath(path);
    const auto separator = normalized.find_last_of(L'\\');
    return separator == std::wstring::npos ? std::wstring() : normalized.substr(0, separator);
}

/**
 * @brief Get the last component of a path below a preset directory.
 * @param[in] path Path of an entry.
 * @return The name of the entry, empty when the path holds none.
 */
std::wstring LeafOf(const std::wstring& path)
{
    const auto normalized = NormalizedPath(path);
    const auto separator = normalized.find_last_of(L'\\');
    return separator == std::wstring::npos ? normalized : normalized.substr(separator + 1);
}

/**
 * @brief Collects the messages which report the entries a capture left out.
 *
 * A capture of a whole machine can leave out thousands of entries, so only the
 * first messages are kept and the rest is summarized by a final one.
 */
class SkippedList
{
public:
    /**
     * @brief Report one entry which was left out.
     * @param[in] message Description of the entry.
     */
    void Add(const std::wstring& message)
    {
        ++total_;
        if (added_ < kMaxSkippedMessages)
        {
            messages_.push_back(message);
            ++added_;
        }
    }

    /**
     * @brief Append the summary of the messages which were dropped.
     * @param[out] report Report which receives the messages.
     */
    void Finish(appbox::SnapshotApplyReport& report) const
    {
        report.skipped.insert(report.skipped.end(), messages_.begin(), messages_.end());
        if (total_ > added_)
        {
            report.skipped.push_back(L"and " + std::to_wstring(total_ - added_) +
                                     L" further entries were left out as well");
        }
    }

private:
    std::vector<std::wstring> messages_;
    std::size_t               added_ = 0;
    std::size_t               total_ = 0;
};

/**
 * @brief Why a folder of the newer snapshot could not be imported.
 */
enum class ImportSkipReason
{
    FolderExisted, ///< The folder of the entry was part of the state before the capture.
    LooseFile      ///< A file was added directly below a preset directory.
};

/**
 * @brief One folder whose entries could not be imported.
 */
struct ImportSkipGroup
{
    std::string      preset_id;
    std::wstring     folder;
    ImportSkipReason reason = ImportSkipReason::FolderExisted;
    std::size_t      count = 0;
};

/**
 * @brief Find the group of a folder which could not be imported.
 * @param[in,out] groups The groups collected so far.
 * @param[in] preset_id Identifier of the preset directory.
 * @param[in] folder Name of the folder below the preset directory.
 * @param[in] reason Why the entries of the folder cannot be imported.
 * @return The group which receives the entry.
 */
ImportSkipGroup& GroupOf(std::vector<ImportSkipGroup>& groups, const std::string& preset_id, const std::wstring& folder,
                         ImportSkipReason reason)
{
    for (auto& group : groups)
    {
        if (group.preset_id == preset_id && EqualsIgnoreCase(group.folder, folder) && group.reason == reason)
        {
            return group;
        }
    }

    ImportSkipGroup group;
    group.preset_id = preset_id;
    group.folder = folder;
    group.reason = reason;
    groups.push_back(std::move(group));
    return groups.back();
}

/**
 * @brief Build the message of a folder whose entries were left out.
 * @param[in] group The group to describe.
 * @return The message.
 */
std::wstring MessageOf(const ImportSkipGroup& group)
{
    if (group.reason == ImportSkipReason::LooseFile)
    {
        return L"'" + group.folder +
               L"' was added directly below its preset directory, and a single file cannot be "
               L"imported on its own";
    }

    return L"'" + group.folder + L"' was part of the state before the capture, so the " + std::to_wstring(group.count) +
           L" entries below it were not imported";
}

/**
 * @brief Import the folders the newer snapshot added.
 *
 * Only a folder which is new below a preset directory can be imported: the
 * archive carries whole folders, so an entry which was added to a folder the
 * capture did not see appear cannot travel on its own.
 *
 * @param[in] diff The difference to apply.
 * @param[in,out] model Model which receives the imports.
 * @param[in,out] skipped List which receives the entries which were left out.
 * @param[out] report Report which counts the imports.
 */
void ApplyAddedFiles(const appbox::SnapshotDiff& diff, appbox::PackModel& model, SkippedList& skipped,
                     appbox::SnapshotApplyReport& report)
{
    std::unordered_set<std::wstring> new_folders;
    for (const auto& entry : diff.added_files)
    {
        if (entry.directory)
        {
            new_folders.insert(appbox::snapshot::FileLookupKey(entry.preset_id, entry.path));
        }
    }

    std::vector<std::pair<std::string, std::wstring>> candidates;
    std::unordered_set<std::wstring>                  seen;
    std::vector<ImportSkipGroup>                      groups;

    for (const auto& entry : diff.added_files)
    {
        const auto folder = FirstComponent(entry.path);
        if (new_folders.count(appbox::snapshot::FileLookupKey(entry.preset_id, folder)) != 0)
        {
            const auto key = appbox::snapshot::FileLookupKey(entry.preset_id, folder);
            if (seen.insert(key).second)
            {
                candidates.emplace_back(entry.preset_id, folder);
            }
            continue;
        }

        const auto reason =
            folder == NormalizedPath(entry.path) ? ImportSkipReason::LooseFile : ImportSkipReason::FolderExisted;
        ++GroupOf(groups, entry.preset_id, folder, reason).count;
    }

    for (const auto& group : groups)
    {
        skipped.Add(MessageOf(group));
    }

    for (const auto& candidate : candidates)
    {
        appbox::PresetDirectory preset;
        if (!appbox::FindPresetDirectory(candidate.first, preset))
        {
            skipped.Add(L"'" + candidate.second +
                        L"' belongs to a preset directory which this machine does not "
                        L"offer, so it was not imported");
            continue;
        }

        appbox::ImportedFolder existing;
        if (model.GetImport(candidate.first, candidate.second, existing))
        {
            /* The folder is part of the archive already, so there is nothing to do. */
            continue;
        }

        const auto  source = (std::filesystem::path(preset.real_path) / candidate.second).wstring();
        std::string detail;
        if (!model.ImportFolder(candidate.first, source, detail))
        {
            skipped.Add(L"'" + candidate.second + L"' could not be imported: " + appbox::UTF8ToWide(detail));
            continue;
        }

        ++report.imported_folders;
    }
}

/**
 * @brief Hide the entries the newer snapshot does not hold anymore.
 *
 * @param[in] diff The difference to apply.
 * @param[in,out] model Model which receives the hidden entries.
 * @param[in,out] skipped List which receives the entries which were left out.
 * @param[out] report Report which counts the hidden entries.
 */
void ApplyRemovedFiles(const appbox::SnapshotDiff& diff, appbox::PackModel& model, SkippedList& skipped,
                       appbox::SnapshotApplyReport& report)
{
    for (const auto& entry : diff.removed_files)
    {
        const auto name = LeafOf(entry.path);
        if (name.empty())
        {
            skipped.Add(L"an entry of '" + entry.path + L"' carries no name and was not hidden");
            continue;
        }

        std::string detail;
        if (!model.AddWhiteout(entry.preset_id, ParentOf(entry.path), name, detail))
        {
            skipped.Add(L"'" + entry.path + L"' could not be hidden: " + appbox::UTF8ToWide(detail));
            continue;
        }

        ++report.whiteouts;
    }
}

/**
 * @brief Reduce a list of removed keys to the ones which are not below another one.
 *
 * A key which is gone takes its sub keys with it, so hiding the sub keys of a
 * hidden key reports the same change twice.
 *
 * @param[in] paths Paths of the removed keys.
 * @return The shallowest paths of the list.
 */
std::vector<std::wstring> ReducedRemovedKeys(const std::vector<std::wstring>& paths)
{
    auto sorted = paths;
    std::sort(sorted.begin(), sorted.end(), [](const std::wstring& left, const std::wstring& right) {
        return appbox::SplitRegistryPath(left).size() < appbox::SplitRegistryPath(right).size();
    });

    std::vector<std::wstring> result;
    for (const auto& path : sorted)
    {
        const bool covered = std::any_of(result.begin(), result.end(), [&path](const std::wstring& kept) {
            return EqualsIgnoreCase(kept, path) || IsRegistryPathBelow(path, kept);
        });
        if (!covered)
        {
            result.push_back(path);
        }
    }

    return result;
}

/**
 * @brief Apply the registry part of a difference.
 *
 * @param[in] diff The difference to apply.
 * @param[in,out] registry Registry which receives the keys and the values.
 * @param[in] presets Preset directories which are resolved on this machine.
 * @param[in,out] skipped List which receives the entries which were left out.
 * @param[out] report Report which counts the entries which were written.
 */
void ApplyRegistry(const appbox::SnapshotDiff& diff, appbox::RegistryModel& registry,
                   const std::vector<appbox::PresetDirectory>& presets, SkippedList& skipped,
                   appbox::SnapshotApplyReport& report)
{
    /* A key which is gone is hidden, and its sub keys are hidden with it. */
    std::unordered_set<std::wstring> hidden_keys;
    for (const auto& path : ReducedRemovedKeys(diff.removed_registry_keys))
    {
        if (IsEnvironmentKey(path))
        {
            continue;
        }

        std::string detail;
        if (!registry.EnsureKey(path, detail) || !registry.SetKeyIsolation(path, appbox::RegistryKeyIsolation::Hide))
        {
            skipped.Add(L"the registry key '" + path + L"' could not be hidden");
            continue;
        }

        hidden_keys.insert(appbox::snapshot::RegistryKeyLookupKey(path));
        ++report.hidden_registry_entries;
    }

    /* A value which is gone is written with the data of the older snapshot and hidden. */
    for (const auto& value : diff.removed_registry_values)
    {
        if (IsEnvironmentKey(value.path))
        {
            continue;
        }

        const bool covered = std::any_of(hidden_keys.begin(), hidden_keys.end(), [&value](const std::wstring& key) {
            return EqualsIgnoreCase(key, appbox::snapshot::RegistryKeyLookupKey(value.path)) ||
                   IsRegistryPathBelow(appbox::snapshot::RegistryKeyLookupKey(value.path), key);
        });
        if (covered)
        {
            continue;
        }

        appbox::RegistryValueType type = appbox::RegistryValueType::None;
        if (!appbox::ResolveRegistryValueType(value.type, type))
        {
            skipped.Add(L"the registry value '" + value.name + L"' of '" + value.path +
                        L"' carries a type which the workspace cannot store");
            continue;
        }

        std::string detail;
        if (!registry.EnsureKey(value.path, detail) ||
            !registry.SetValue(value.path, value.name, type, value.data, detail) ||
            !registry.SetValueIsolation(value.path, value.name, appbox::RegistryValueIsolation::Hide))
        {
            skipped.Add(L"the registry value '" + value.name + L"' of '" + value.path + L"' could not be hidden");
            continue;
        }

        ++report.hidden_registry_entries;
    }

    /* A key which is new is created before the values below it are written. */
    for (const auto& path : diff.added_registry_keys)
    {
        if (IsEnvironmentKey(path))
        {
            continue;
        }

        std::string detail;
        if (!registry.EnsureKey(path, detail))
        {
            skipped.Add(L"the registry key '" + path + L"' could not be created: " + appbox::UTF8ToWide(detail));
            continue;
        }

        ++report.registry_keys;
    }

    const auto write_value = [&](const appbox::SnapshotDiffRegistryValue& value) {
        if (IsEnvironmentKey(value.path))
        {
            return;
        }

        appbox::RegistryValueType type = appbox::RegistryValueType::None;
        if (!appbox::ResolveRegistryValueType(value.type, type))
        {
            skipped.Add(L"the registry value '" + value.name + L"' of '" + value.path +
                        L"' carries a type which the workspace cannot store");
            return;
        }

        bool       rewritten = false;
        const auto data = appbox::RewriteRegistryValueReferences(value.type, value.data, presets, rewritten);

        std::string detail;
        if (!registry.EnsureKey(value.path, detail) || !registry.SetValue(value.path, value.name, type, data, detail))
        {
            skipped.Add(L"the registry value '" + value.name + L"' of '" + value.path + L"' could not be written: " +
                        appbox::UTF8ToWide(detail));
            return;
        }

        ++report.registry_values;
    };

    for (const auto& value : diff.added_registry_values)
    {
        write_value(value);
    }
    for (const auto& value : diff.modified_registry_values)
    {
        write_value(value);
    }
}

/**
 * @brief Apply the environment part of a difference.
 *
 * @param[in] diff The difference to apply.
 * @param[in,out] environment Environment which receives the variables.
 * @param[in] presets Preset directories which are resolved on this machine.
 * @param[in,out] skipped List which receives the entries which were left out.
 * @param[out] report Report which counts the variables which were written.
 */
void ApplyEnvironment(const appbox::SnapshotDiff& diff, appbox::EnvironmentModel& environment,
                      const std::vector<appbox::PresetDirectory>& presets, SkippedList& skipped,
                      appbox::SnapshotApplyReport& report)
{
    const auto write_entry = [&](const appbox::SnapshotDiffEnvironment& entry) {
        bool rewritten = false;

        appbox::EnvironmentEntry candidate;
        candidate.name = entry.name;
        candidate.value = appbox::RewriteKnownFolderReferences(entry.value, presets, rewritten);

        /*
         * The modes are the ones a variable of the workspace starts with: the
         * application reads the value of the row and not the value of the host.
         */
        candidate.isolation = appbox::EnvironmentIsolation::WriteCopy;
        candidate.merge = appbox::environment_isolation::kDefaultMergeMode;
        candidate.merge_string.clear();

        const auto  index = environment.IndexOfName(candidate.name);
        std::string detail;
        const bool  stored = index < 0 ? environment.AddEntry(candidate, detail)
                                       : environment.SetEntry(static_cast<std::size_t>(index), candidate, detail);
        if (!stored)
        {
            skipped.Add(L"the environment variable '" + candidate.name + L"' could not be written: " +
                        appbox::UTF8ToWide(detail));
            return;
        }

        ++report.environment_entries;
    };

    for (const auto& entry : diff.added_environment)
    {
        write_entry(entry);
    }
    for (const auto& entry : diff.modified_environment)
    {
        write_entry(entry);
    }

    for (const auto& entry : diff.removed_environment)
    {
        /*
         * A row of the environment workspace reports a value and never the
         * absence of a variable, so a removed variable cannot be expressed.
         */
        skipped.Add(L"the environment variable '" + entry.name +
                    L"' was removed, which a row of the environment workspace cannot express");
    }
}

} // namespace

namespace appbox
{

bool ApplySnapshotDiff(const SnapshotDiff& diff, PackModel& model, RegistryModel& registry,
                       EnvironmentModel& environment, SnapshotApplyReport& report, std::string& error)
{
    /* A difference which cannot be interpreted is refused before anything is written. */
    for (const auto& entry : diff.added_files)
    {
        if (entry.preset_id.empty() || !IsUsablePath(entry.path))
        {
            error = "the difference holds a filesystem entry which cannot be interpreted";
            return false;
        }
    }
    for (const auto& entry : diff.modified_files)
    {
        if (entry.preset_id.empty() || !IsUsablePath(entry.path))
        {
            error = "the difference holds a filesystem entry which cannot be interpreted";
            return false;
        }
    }
    for (const auto& entry : diff.removed_files)
    {
        if (entry.preset_id.empty() || !IsUsablePath(entry.path))
        {
            error = "the difference holds a filesystem entry which cannot be interpreted";
            return false;
        }
    }

    const auto presets = PresetDirectories();

    SkippedList skipped;
    ApplyAddedFiles(diff, model, skipped, report);
    ApplyRemovedFiles(diff, model, skipped, report);
    ApplyRegistry(diff, registry, presets, skipped, report);
    ApplyEnvironment(diff, environment, presets, skipped, report);
    skipped.Finish(report);

    return true;
}

std::wstring SnapshotApplySummary(const SnapshotDiff& diff, const SnapshotApplyReport& report)
{
    std::wstring text;

    text += L"The difference of the two snapshots:\n";
    text += L"  filesystem: " + std::to_wstring(diff.added_files.size()) + L" added, " +
            std::to_wstring(diff.modified_files.size()) + L" modified, " + std::to_wstring(diff.removed_files.size()) +
            L" removed\n";
    text += L"  registry: " + std::to_wstring(diff.added_registry_keys.size()) + L" keys added, " +
            std::to_wstring(diff.removed_registry_keys.size()) + L" keys removed, " +
            std::to_wstring(diff.added_registry_values.size()) + L" values added, " +
            std::to_wstring(diff.modified_registry_values.size()) + L" values modified, " +
            std::to_wstring(diff.removed_registry_values.size()) + L" values removed\n";
    text += L"  environment: " + std::to_wstring(diff.added_environment.size()) + L" added, " +
            std::to_wstring(diff.modified_environment.size()) + L" modified, " +
            std::to_wstring(diff.removed_environment.size()) + L" removed\n";

    text += L"\nWritten into the workspaces:\n";
    text += L"  imported folders: " + std::to_wstring(report.imported_folders) + L"\n";
    text += L"  hidden entries: " + std::to_wstring(report.whiteouts) + L"\n";
    text += L"  registry keys: " + std::to_wstring(report.registry_keys) + L"\n";
    text += L"  registry values: " + std::to_wstring(report.registry_values) + L"\n";
    text += L"  hidden registry entries: " + std::to_wstring(report.hidden_registry_entries) + L"\n";
    text += L"  environment variables: " + std::to_wstring(report.environment_entries) + L"\n";

    if (!report.skipped.empty())
    {
        text += L"\nLeft out:\n";
        for (const auto& message : report.skipped)
        {
            text += L"  " + message + L"\n";
        }
    }

    return text;
}

} // namespace appbox
