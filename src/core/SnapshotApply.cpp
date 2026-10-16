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
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{

/**
 * @brief Number of the messages a list of the report holds before it counts the
 *        rest.
 */
constexpr std::size_t kMaxNoticeMessages = 20;

/**
 * @brief What the fill reports while it writes the files of a difference.
 */
constexpr const wchar_t* kFilesystemAction = L"Writing the files of the capture";

/**
 * @brief What the fill reports while it writes the registry of a difference.
 */
constexpr const wchar_t* kRegistryAction = L"Writing the registry of the capture";

/**
 * @brief What the fill reports while it writes the environment of a difference.
 */
constexpr const wchar_t* kEnvironmentAction = L"Writing the environment of the capture";

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
 * @brief Whether a path is below another one.
 *
 * The ancestor has to be a whole component prefix, so `MyAppData` is not below
 * `MyApp`, and the names are compared without regard to the case, the way the
 * filesystem and the registry of Windows compare them.
 *
 * @param[in] path Path to test.
 * @param[in] ancestor Path of the folder to test against.
 * @return true when the path is a strict descendant of the ancestor.
 */
bool IsPathBelow(const std::wstring& path, const std::wstring& ancestor)
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
 * @brief Whether a registry path is a key below another one.
 * @param[in] path Path to test.
 * @param[in] ancestor Key to test against.
 * @return true when the path is a strict descendant of the ancestor.
 */
bool IsRegistryPathBelow(const std::wstring& path, const std::wstring& ancestor)
{
    return IsPathBelow(path, ancestor);
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
 * @brief Normalize a path of a difference.
 * @param[in] path Path to normalize.
 * @return The path with backslashes.
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
 * @brief Whether a path of a difference can be interpreted.
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
 * @brief Name a path of the view the way the workspaces show it.
 * @param[in] preset_id Identifier of the preset directory.
 * @param[in] path Path relative to the preset directory.
 * @return The path of the preset directory extended by the path of the entry.
 */
std::wstring ViewPathOf(const std::string& preset_id, const std::wstring& path)
{
    appbox::PresetDirectory preset;
    if (!appbox::FindPresetDirectory(preset_id, preset))
    {
        return path;
    }

    return path.empty() ? preset.display_name : preset.display_name + L"\\" + path;
}

/**
 * @brief Name a registry entry the way the workspace shows it.
 * @param[in] path Path of the key.
 * @param[in] name Name of the value, empty for the key itself.
 * @return The name of the entry.
 */
std::wstring RegistryObjectOf(const std::wstring& path, const std::wstring& name)
{
    return name.empty() ? path : path + L"\\" + name;
}

/**
 * @brief Build the message of an entry which carries no name.
 * @param[in] path Path of the entry.
 * @return The message.
 */
std::wstring MissingNameMessage(const std::wstring& path)
{
    return L"an entry of '" + path + L"' carries no name and was not hidden";
}

/**
 * @brief Build the message of a registry value the workspace cannot store.
 * @param[in] path Path of the key which holds the value.
 * @param[in] name Name of the value.
 * @return The message.
 */
std::wstring UnsupportedTypeMessage(const std::wstring& path, const std::wstring& name)
{
    return L"the registry value '" + name + L"' of '" + path + L"' carries a type which the workspace cannot store";
}

/**
 * @brief Build the message of a variable which was removed.
 * @param[in] name Name of the variable.
 * @return The message.
 */
std::wstring RemovedVariableMessage(const std::wstring& name)
{
    return L"the environment variable '" + name +
           L"' was removed, which a row of the environment workspace cannot express";
}

/**
 * @brief Report the state of one entry to the caller.
 * @param[in] progress Callback of the caller, may be empty.
 * @param[in] domain The part of the machine the entry belongs to.
 * @param[in] action What the fill is doing.
 * @param[in] object The entry which is being written.
 * @param[in] state The state of the entry.
 * @param[in] message The reason of a failure, empty otherwise.
 */
void Report(const appbox::CaptureFillProgressCallback& progress, appbox::CaptureFillDomain domain,
            const std::wstring& action, const std::wstring& object, appbox::CaptureFillState state,
            const std::wstring& message = {})
{
    if (!progress)
    {
        return;
    }

    appbox::CaptureFillProgress report;
    report.domain = domain;
    report.action = action;
    report.object = object;
    report.state = state;
    report.message = message;
    progress(report);
}

/**
 * @brief Collects the entries the fill did not write.
 *
 * A capture of a whole machine can leave out thousands of entries, so only the
 * first entries are kept and the count of all of them is reported next to them.
 */
class NoticeList
{
public:
    /**
     * @brief Report one entry which was not written.
     * @param[in] domain The part of the machine the entry belongs to.
     * @param[in] message Description of the entry and of the reason.
     */
    void Add(appbox::CaptureFillDomain domain, const std::wstring& message)
    {
        ++total_;
        if (added_ < kMaxNoticeMessages)
        {
            appbox::CaptureFillNotice notice;
            notice.domain = domain;
            notice.message = message;
            entries_.push_back(std::move(notice));
            ++added_;
        }
    }

    /**
     * @brief Add the entries and their count to the report.
     *
     * The list of the report is capped, so the entries of a caller which
     * reported first keep their place and the entries behind them are counted
     * alone.
     *
     * @param[in,out] entries List of the report.
     * @param[in,out] total Count of the report.
     */
    void Finish(std::vector<appbox::CaptureFillNotice>& entries, std::size_t& total) const
    {
        for (const auto& entry : entries_)
        {
            if (entries.size() < kMaxNoticeMessages)
            {
                entries.push_back(entry);
            }
        }

        total += total_;
    }

private:
    std::vector<appbox::CaptureFillNotice> entries_;
    std::size_t                            added_ = 0;
    std::size_t                            total_ = 0;
};

/**
 * @brief Whether a difference can be interpreted.
 * @param[in] diff The difference to check.
 * @param[out] error Error description when the difference is not usable.
 * @return true when the difference is usable.
 */
bool ValidateDiff(const appbox::SnapshotDiff& diff, std::string& error)
{
    for (const auto* entries : { &diff.added_files, &diff.modified_files, &diff.removed_files })
    {
        for (const auto& entry : *entries)
        {
            if (entry.preset_id.empty() || !IsUsablePath(entry.path))
            {
                error = "the difference holds a filesystem entry which cannot be interpreted";
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Reduce the removed entries to the ones which are not below another one.
 *
 * A folder which is gone takes everything below it with it, so the marker of
 * the folder hides the entries below it as well and those entries are left out:
 * a capture which reports the delete of a folder and of its content describes
 * one change and not thousands of them.
 *
 * @param[in] entries The entries the newer state does not hold anymore.
 * @return The entries which need a marker of their own.
 */
std::vector<appbox::SnapshotDiffFile> ReducedRemovedPaths(const std::vector<appbox::SnapshotDiffFile>& entries)
{
    auto sorted = entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const appbox::SnapshotDiffFile& left, const appbox::SnapshotDiffFile& right) {
                  const auto left_depth = appbox::Split(NormalizedPath(left.path), L"\\").size();
                  const auto right_depth = appbox::Split(NormalizedPath(right.path), L"\\").size();
                  if (left_depth != right_depth)
                  {
                      return left_depth < right_depth;
                  }
                  if (left.preset_id != right.preset_id)
                  {
                      return left.preset_id < right.preset_id;
                  }
                  return NormalizedPath(left.path) < NormalizedPath(right.path);
              });

    std::vector<appbox::SnapshotDiffFile> result;
    for (const auto& entry : sorted)
    {
        const auto path = NormalizedPath(entry.path);
        const bool covered = std::any_of(result.begin(), result.end(), [&](const appbox::SnapshotDiffFile& kept) {
            return kept.preset_id == entry.preset_id && IsPathBelow(path, NormalizedPath(kept.path));
        });
        if (!covered)
        {
            result.push_back(entry);
        }
    }

    return result;
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
 * @brief Ensure that a folder of the view holds a path.
 *
 * The folders of the way to a path are created from the top down, so every
 * parent is a folder of the view while the next one is created: a capture fills
 * a file below a folder which existed before the capture just as well as one
 * below a folder it created.
 *
 * @param[in,out] model Model which receives the folders.
 * @param[in] preset_id Identifier of the preset directory.
 * @param[in] directory Path of the folder relative to the preset directory,
 *            empty for the layer root.
 * @param[out] error Error description on failure.
 * @return true when the folder is a folder of the view.
 */
bool EnsureFolders(appbox::PackModel& model, const std::string& preset_id, const std::wstring& directory,
                   std::string& error)
{
    if (directory.empty() || model.IsDirectoryOfView(preset_id, directory))
    {
        return true;
    }

    std::wstring reached;
    for (const auto& segment : appbox::Split(directory, L"\\"))
    {
        if (segment.empty())
        {
            continue;
        }

        reached = reached.empty() ? segment : reached + L"\\" + segment;
        if (model.IsDirectoryOfView(preset_id, reached))
        {
            continue;
        }

        if (!model.RestoreVirtualDirectory(preset_id, reached, error))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Write the files the newer state added or whose content changed.
 *
 * The entries are written at the very path they were captured at: the folders
 * of the way to a file are created as folders of the view and the file is
 * imported on its own, so nothing around the entry travels into the archive.
 *
 * @param[in] diff The difference to write.
 * @param[in,out] model Model which receives the files and the folders.
 * @param[in,out] ignored List which receives the entries which cannot be expressed.
 * @param[in,out] errors List which receives the entries which could not be written.
 * @param[in] progress Callback which receives the work of the fill.
 * @param[out] report Report which counts the entries which were written.
 */
void ApplyAddedFiles(const appbox::SnapshotDiff& diff, appbox::PackModel& model, NoticeList& ignored,
                     NoticeList& errors, const appbox::CaptureFillProgressCallback& progress,
                     appbox::CaptureFillReport& report)
{
    const auto place = [&](const appbox::SnapshotDiffFile& entry) {
        const auto path = NormalizedPath(entry.path);
        const auto object = ViewPathOf(entry.preset_id, path);

        appbox::PresetDirectory preset;
        if (!appbox::FindPresetDirectory(entry.preset_id, preset))
        {
            errors.Add(appbox::CaptureFillDomain::Filesystem,
                       L"'" + object +
                           L"' belongs to a preset directory which this machine does not offer, so it was not "
                           L"written");
            return;
        }

        Report(progress, appbox::CaptureFillDomain::Filesystem, kFilesystemAction, object,
               appbox::CaptureFillState::Working);

        const auto directory = ParentOf(path);
        const auto name = LeafOf(path);
        if (name.empty())
        {
            Report(progress, appbox::CaptureFillDomain::Filesystem, kFilesystemAction, object,
                   appbox::CaptureFillState::Failed, MissingNameMessage(path));
            ignored.Add(appbox::CaptureFillDomain::Filesystem, MissingNameMessage(path));
            return;
        }

        std::string detail;
        if (entry.directory)
        {
            /*
             * A folder is an entry of the view: the folders of the way to it and
             * the folder itself. A folder the view carries already needs no work,
             * so it is not counted as one which was written.
             */
            const bool known = model.IsDirectoryOfView(entry.preset_id, path);
            if (!known && !EnsureFolders(model, entry.preset_id, path, detail))
            {
                Report(progress, appbox::CaptureFillDomain::Filesystem, kFilesystemAction, object,
                       appbox::CaptureFillState::Failed, appbox::UTF8ToWide(detail));
                errors.Add(appbox::CaptureFillDomain::Filesystem,
                           L"'" + object + L"' could not be created: " + appbox::UTF8ToWide(detail));
                return;
            }

            if (!known)
            {
                ++report.applied;
            }

            Report(progress, appbox::CaptureFillDomain::Filesystem, kFilesystemAction, object,
                   appbox::CaptureFillState::Succeeded);
            return;
        }

        /*
         * An entry which the view carries already is left where it is: an
         * imported folder travels into the archive as the whole host folder it
         * names, and a file which was imported before is part of the view.
         */
        if (model.HasImportedFileAt(entry.preset_id, path) || model.HostImportCarries(entry.preset_id, path))
        {
            Report(progress, appbox::CaptureFillDomain::Filesystem, kFilesystemAction, object,
                   appbox::CaptureFillState::Succeeded);
            return;
        }

        if (!EnsureFolders(model, entry.preset_id, directory, detail))
        {
            Report(progress, appbox::CaptureFillDomain::Filesystem, kFilesystemAction, object,
                   appbox::CaptureFillState::Failed, appbox::UTF8ToWide(detail));
            errors.Add(appbox::CaptureFillDomain::Filesystem,
                       L"'" + object + L"' could not be written: " + appbox::UTF8ToWide(detail));
            return;
        }

        /*
         * The archive reads the host file while the session is packed, so an
         * entry which is gone in the meantime cannot travel.
         */
        const auto      source = (std::filesystem::path(preset.real_path) / path).wstring();
        std::error_code ec;
        if (!std::filesystem::is_regular_file(source, ec))
        {
            errors.Add(appbox::CaptureFillDomain::Filesystem,
                       L"'" + object + L"' is not a file of the host anymore, so it was not written");
            Report(progress, appbox::CaptureFillDomain::Filesystem, kFilesystemAction, object,
                   appbox::CaptureFillState::Failed, L"the file is not on the host anymore");
            return;
        }

        if (!model.RestoreImportedFile(entry.preset_id, directory, name, source, detail))
        {
            Report(progress, appbox::CaptureFillDomain::Filesystem, kFilesystemAction, object,
                   appbox::CaptureFillState::Failed, appbox::UTF8ToWide(detail));
            errors.Add(appbox::CaptureFillDomain::Filesystem,
                       L"'" + object + L"' could not be written: " + appbox::UTF8ToWide(detail));
            return;
        }

        ++report.applied;
        Report(progress, appbox::CaptureFillDomain::Filesystem, kFilesystemAction, object,
               appbox::CaptureFillState::Succeeded);
    };

    for (const auto& entry : diff.added_files)
    {
        place(entry);
    }
    for (const auto& entry : diff.modified_files)
    {
        place(entry);
    }
}

/**
 * @brief Hide the entries the newer state does not hold anymore.
 *
 * @param[in] diff The difference to write.
 * @param[in,out] model Model which receives the hidden entries.
 * @param[in,out] ignored List which receives the entries which cannot be expressed.
 * @param[in,out] errors List which receives the entries which could not be written.
 * @param[in] progress Callback which receives the work of the fill.
 * @param[out] report Report which counts the entries which were written.
 */
void ApplyRemovedFiles(const appbox::SnapshotDiff& diff, appbox::PackModel& model, NoticeList& ignored,
                       NoticeList& errors, const appbox::CaptureFillProgressCallback& progress,
                       appbox::CaptureFillReport& report)
{
    for (const auto& entry : ReducedRemovedPaths(diff.removed_files))
    {
        const auto path = NormalizedPath(entry.path);
        const auto object = ViewPathOf(entry.preset_id, path);

        appbox::PresetDirectory preset;
        if (!appbox::FindPresetDirectory(entry.preset_id, preset))
        {
            errors.Add(appbox::CaptureFillDomain::Filesystem,
                       L"'" + object +
                           L"' belongs to a preset directory which this machine does not offer, so it was not "
                           L"hidden");
            continue;
        }

        Report(progress, appbox::CaptureFillDomain::Filesystem, kFilesystemAction, object,
               appbox::CaptureFillState::Working);

        const auto name = LeafOf(path);
        if (name.empty())
        {
            ignored.Add(appbox::CaptureFillDomain::Filesystem, MissingNameMessage(path));
            Report(progress, appbox::CaptureFillDomain::Filesystem, kFilesystemAction, object,
                   appbox::CaptureFillState::Failed, MissingNameMessage(path));
            continue;
        }

        if (model.HasWhiteout(entry.preset_id, path))
        {
            /* The entry is hidden already, so there is nothing to write. */
            Report(progress, appbox::CaptureFillDomain::Filesystem, kFilesystemAction, object,
                   appbox::CaptureFillState::Succeeded);
            continue;
        }

        std::string detail;
        if (!model.AddWhiteout(entry.preset_id, ParentOf(path), name, detail))
        {
            errors.Add(appbox::CaptureFillDomain::Filesystem,
                       L"'" + object + L"' could not be hidden: " + appbox::UTF8ToWide(detail));
            Report(progress, appbox::CaptureFillDomain::Filesystem, kFilesystemAction, object,
                   appbox::CaptureFillState::Failed, appbox::UTF8ToWide(detail));
            continue;
        }

        ++report.applied;
        Report(progress, appbox::CaptureFillDomain::Filesystem, kFilesystemAction, object,
               appbox::CaptureFillState::Succeeded);
    }
}

/**
 * @brief Write the registry part of a difference.
 *
 * @param[in] diff The difference to write.
 * @param[in,out] registry Registry which receives the keys and the values.
 * @param[in] presets Preset directories which are resolved on this machine.
 * @param[in,out] ignored List which receives the entries which cannot be expressed.
 * @param[in,out] errors List which receives the entries which could not be written.
 * @param[in] progress Callback which receives the work of the fill.
 * @param[out] report Report which counts the entries which were written.
 */
void ApplyRegistry(const appbox::SnapshotDiff& diff, appbox::RegistryModel& registry,
                   const std::vector<appbox::PresetDirectory>& presets, NoticeList& ignored, NoticeList& errors,
                   const appbox::CaptureFillProgressCallback& progress, appbox::CaptureFillReport& report)
{
    /* A key which is gone is hidden, and its sub keys are hidden with it. */
    std::unordered_set<std::wstring> hidden_keys;
    for (const auto& path : ReducedRemovedKeys(diff.removed_registry_keys))
    {
        if (IsEnvironmentKey(path))
        {
            continue;
        }

        const auto object = RegistryObjectOf(path, std::wstring());
        Report(progress, appbox::CaptureFillDomain::Registry, kRegistryAction, object,
               appbox::CaptureFillState::Working);

        std::string detail;
        if (!registry.EnsureKey(path, detail) || !registry.SetKeyIsolation(path, appbox::RegistryKeyIsolation::Hide))
        {
            errors.Add(appbox::CaptureFillDomain::Registry,
                       L"the registry key '" + path + L"' could not be hidden: " + appbox::UTF8ToWide(detail));
            Report(progress, appbox::CaptureFillDomain::Registry, kRegistryAction, object,
                   appbox::CaptureFillState::Failed, appbox::UTF8ToWide(detail));
            continue;
        }

        hidden_keys.insert(appbox::snapshot::RegistryKeyLookupKey(path));
        ++report.applied;
        Report(progress, appbox::CaptureFillDomain::Registry, kRegistryAction, object,
               appbox::CaptureFillState::Succeeded);
    }

    /* A value which is gone is written with the data of the older state and hidden. */
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

        const auto object = RegistryObjectOf(value.path, value.name);
        Report(progress, appbox::CaptureFillDomain::Registry, kRegistryAction, object,
               appbox::CaptureFillState::Working);

        appbox::RegistryValueType type = appbox::RegistryValueType::None;
        if (!appbox::ResolveRegistryValueType(value.type, type))
        {
            ignored.Add(appbox::CaptureFillDomain::Registry, UnsupportedTypeMessage(value.path, value.name));
            Report(progress, appbox::CaptureFillDomain::Registry, kRegistryAction, object,
                   appbox::CaptureFillState::Failed, UnsupportedTypeMessage(value.path, value.name));
            continue;
        }

        std::string detail;
        if (!registry.EnsureKey(value.path, detail) ||
            !registry.SetValue(value.path, value.name, type, value.data, detail) ||
            !registry.SetValueIsolation(value.path, value.name, appbox::RegistryValueIsolation::Hide))
        {
            errors.Add(appbox::CaptureFillDomain::Registry, L"the registry value '" + value.name + L"' of '" +
                                                                value.path + L"' could not be hidden: " +
                                                                appbox::UTF8ToWide(detail));
            Report(progress, appbox::CaptureFillDomain::Registry, kRegistryAction, object,
                   appbox::CaptureFillState::Failed, appbox::UTF8ToWide(detail));
            continue;
        }

        ++report.applied;
        Report(progress, appbox::CaptureFillDomain::Registry, kRegistryAction, object,
               appbox::CaptureFillState::Succeeded);
    }

    /* A key which is new is created before the values below it are written. */
    for (const auto& path : diff.added_registry_keys)
    {
        if (IsEnvironmentKey(path))
        {
            continue;
        }

        const auto object = RegistryObjectOf(path, std::wstring());
        Report(progress, appbox::CaptureFillDomain::Registry, kRegistryAction, object,
               appbox::CaptureFillState::Working);

        std::string detail;
        if (!registry.EnsureKey(path, detail))
        {
            errors.Add(appbox::CaptureFillDomain::Registry,
                       L"the registry key '" + path + L"' could not be created: " + appbox::UTF8ToWide(detail));
            Report(progress, appbox::CaptureFillDomain::Registry, kRegistryAction, object,
                   appbox::CaptureFillState::Failed, appbox::UTF8ToWide(detail));
            continue;
        }

        ++report.applied;
        Report(progress, appbox::CaptureFillDomain::Registry, kRegistryAction, object,
               appbox::CaptureFillState::Succeeded);
    }

    const auto write_value = [&](const appbox::SnapshotDiffRegistryValue& value) {
        if (IsEnvironmentKey(value.path))
        {
            return;
        }

        const auto object = RegistryObjectOf(value.path, value.name);
        Report(progress, appbox::CaptureFillDomain::Registry, kRegistryAction, object,
               appbox::CaptureFillState::Working);

        appbox::RegistryValueType type = appbox::RegistryValueType::None;
        if (!appbox::ResolveRegistryValueType(value.type, type))
        {
            ignored.Add(appbox::CaptureFillDomain::Registry, UnsupportedTypeMessage(value.path, value.name));
            Report(progress, appbox::CaptureFillDomain::Registry, kRegistryAction, object,
                   appbox::CaptureFillState::Failed, UnsupportedTypeMessage(value.path, value.name));
            return;
        }

        bool       rewritten = false;
        const auto data = appbox::RewriteRegistryValueReferences(value.type, value.data, presets, rewritten);

        std::string detail;
        if (!registry.EnsureKey(value.path, detail) || !registry.SetValue(value.path, value.name, type, data, detail))
        {
            errors.Add(appbox::CaptureFillDomain::Registry, L"the registry value '" + value.name + L"' of '" +
                                                                value.path + L"' could not be written: " +
                                                                appbox::UTF8ToWide(detail));
            Report(progress, appbox::CaptureFillDomain::Registry, kRegistryAction, object,
                   appbox::CaptureFillState::Failed, appbox::UTF8ToWide(detail));
            return;
        }

        ++report.applied;
        Report(progress, appbox::CaptureFillDomain::Registry, kRegistryAction, object,
               appbox::CaptureFillState::Succeeded);
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
 * @brief Write the environment part of a difference.
 *
 * @param[in] diff The difference to write.
 * @param[in,out] environment Environment which receives the variables.
 * @param[in] presets Preset directories which are resolved on this machine.
 * @param[in,out] ignored List which receives the entries which cannot be expressed.
 * @param[in,out] errors List which receives the entries which could not be written.
 * @param[in] progress Callback which receives the work of the fill.
 * @param[out] report Report which counts the entries which were written.
 */
void ApplyEnvironment(const appbox::SnapshotDiff& diff, appbox::EnvironmentModel& environment,
                      const std::vector<appbox::PresetDirectory>& presets, NoticeList& ignored, NoticeList& errors,
                      const appbox::CaptureFillProgressCallback& progress, appbox::CaptureFillReport& report)
{
    const auto write_entry = [&](const appbox::SnapshotDiffEnvironment& entry) {
        Report(progress, appbox::CaptureFillDomain::Environment, kEnvironmentAction, entry.name,
               appbox::CaptureFillState::Working);

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
            errors.Add(appbox::CaptureFillDomain::Environment, L"the environment variable '" + candidate.name +
                                                                   L"' could not be written: " +
                                                                   appbox::UTF8ToWide(detail));
            Report(progress, appbox::CaptureFillDomain::Environment, kEnvironmentAction, entry.name,
                   appbox::CaptureFillState::Failed, appbox::UTF8ToWide(detail));
            return;
        }

        ++report.applied;
        Report(progress, appbox::CaptureFillDomain::Environment, kEnvironmentAction, entry.name,
               appbox::CaptureFillState::Succeeded);
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
        ignored.Add(appbox::CaptureFillDomain::Environment, RemovedVariableMessage(entry.name));
        Report(progress, appbox::CaptureFillDomain::Environment, kEnvironmentAction, entry.name,
               appbox::CaptureFillState::Failed, RemovedVariableMessage(entry.name));
    }
}

} // namespace

namespace appbox
{

void AddIgnoredNotice(CaptureFillReport& report, CaptureFillDomain domain, const std::wstring& message)
{
    ++report.total_ignored;
    if (report.ignored.size() >= kMaxNoticeMessages)
    {
        return;
    }

    CaptureFillNotice notice;
    notice.domain = domain;
    notice.message = message;
    report.ignored.push_back(std::move(notice));
}

void AddErrorNotice(CaptureFillReport& report, CaptureFillDomain domain, const std::wstring& message)
{
    ++report.total_errors;
    if (report.errors.size() >= kMaxNoticeMessages)
    {
        return;
    }

    CaptureFillNotice notice;
    notice.domain = domain;
    notice.message = message;
    report.errors.push_back(std::move(notice));
}

bool ApplySnapshotDiff(const SnapshotDiff& diff, PackModel& model, RegistryModel& registry,
                       EnvironmentModel& environment, CaptureFillReport& report,
                       const CaptureFillProgressCallback& progress, std::string& error)
{
    /* A difference which cannot be interpreted is refused before anything is written. */
    if (!ValidateDiff(diff, error))
    {
        return false;
    }

    const auto presets = PresetDirectories();

    NoticeList ignored;
    NoticeList errors;

    ApplyAddedFiles(diff, model, ignored, errors, progress, report);
    ApplyRemovedFiles(diff, model, ignored, errors, progress, report);
    ApplyRegistry(diff, registry, presets, ignored, errors, progress, report);
    ApplyEnvironment(diff, environment, presets, ignored, errors, progress, report);

    ignored.Finish(report.ignored, report.total_ignored);
    errors.Finish(report.errors, report.total_errors);
    return true;
}

} // namespace appbox
