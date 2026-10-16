#include "PackModel.hpp"
#include "FilesystemMarker.hpp"
#include "StartupFileType.hpp"
#include "WString.hpp"
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <system_error>

namespace
{

/**
 * @brief Case insensitive wide string comparison.
 * @param[in] a Left operand.
 * @param[in] b Right operand.
 * @return true when both strings are equal ignoring case.
 */
bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (std::towlower(a[i]) != std::towlower(b[i]))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Normalize a directory relative to a preset directory.
 *
 * Forward slashes are converted to backslashes, empty segments are dropped
 * and leading separators are removed. Drive relative paths and parent
 * references are rejected because the result is used as a path below a
 * preset directory.
 *
 * @param[in] dir Directory to normalize.
 * @return The normalized directory, empty when it is not usable.
 */
std::wstring NormalizeRelativeDir(const std::wstring& dir)
{
    std::wstring converted;
    converted.reserve(dir.size());
    for (const auto ch : dir)
    {
        converted.push_back(ch == L'/' ? L'\\' : ch);
    }

    if (converted.size() >= 2 && converted[1] == L':')
    {
        return {};
    }

    std::wstring result;
    for (const auto& part : appbox::Split(converted, L"\\"))
    {
        if (part.empty())
        {
            continue;
        }
        if (part == L"." || part == L"..")
        {
            return {};
        }

        if (!result.empty())
        {
            result.push_back(L'\\');
        }
        result += part;
    }

    return result;
}

/**
 * @brief Whether a string is usable as a single file or folder name.
 *
 * A usable name is neither empty nor the current or the parent directory
 * reference and holds no path separator, so it always stays inside the
 * directory it is stored in.
 *
 * @param[in] name Name to check.
 * @return true when the name is usable.
 */
bool IsPlainName(const std::wstring& name)
{
    if (name.empty() || name == L"." || name == L"..")
    {
        return false;
    }

    return name.find(L'\\') == std::wstring::npos && name.find(L'/') == std::wstring::npos;
}

/**
 * @brief Normalize a file path relative to an imported folder.
 *
 * Forward slashes are converted to backslashes and empty segments are
 * dropped. Drive relative paths and parent references are rejected because
 * the result is used as a path below an imported folder.
 *
 * @param[in] path Path to normalize.
 * @return The normalized path, empty when it is not usable.
 */
std::wstring NormalizeRelativeFile(const std::wstring& path)
{
    std::wstring converted;
    converted.reserve(path.size());
    for (const auto ch : path)
    {
        converted.push_back(ch == L'/' ? L'\\' : ch);
    }

    if (converted.size() >= 2 && converted[1] == L':')
    {
        return {};
    }

    std::wstring result;
    for (const auto& part : appbox::Split(converted, L"\\"))
    {
        if (part.empty() || part == L".")
        {
            continue;
        }
        if (part == L"..")
        {
            return {};
        }

        if (!result.empty())
        {
            result.push_back(L'\\');
        }
        result += part;
    }

    return result;
}

/**
 * @brief Join the segments of a normalized relative directory.
 * @param[in] segments Segments to join.
 * @param[in] first Index of the first segment to include.
 * @return The joined directory, empty when no segment remains.
 */
std::wstring JoinSegments(const std::vector<std::wstring>& segments, std::size_t first)
{
    std::wstring result;
    for (auto i = first; i < segments.size(); ++i)
    {
        if (segments[i].empty())
        {
            continue;
        }
        if (!result.empty())
        {
            result.push_back(L'\\');
        }
        result += segments[i];
    }
    return result;
}

/**
 * @brief Get the directory a normalized relative path lives in.
 * @param[in] path Path to split.
 * @return The parent directory, empty when the path has no separator.
 */
std::wstring ParentDirectory(const std::wstring& path)
{
    const auto separator = path.find_last_of(L'\\');
    return separator == std::wstring::npos ? std::wstring() : path.substr(0, separator);
}

/**
 * @brief Get the last component of a normalized relative path.
 * @param[in] path Path to split.
 * @return The name of the entry.
 */
std::wstring LeafName(const std::wstring& path)
{
    const auto separator = path.find_last_of(L'\\');
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

/**
 * @brief Whether a path names an entry below another one.
 *
 * The ancestor has to be a whole component prefix, so `MyAppData` is not below
 * `MyApp`.
 *
 * @param[in] path Path of the entry to test.
 * @param[in] ancestor Path of the directory to test against.
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
 * @brief Whether a name is one of the names the view reserves for its markers.
 *
 * The marker of an entry is a sibling of the entry, so a name which carries
 * the suffix of a whiteout, and the name of the opaque marker itself, name the
 * view rather than an entry it holds.
 *
 * @param[in] name Name to check.
 * @return true when the name is reserved.
 */
bool IsMarkerName(const std::wstring& name)
{
    if (EqualsIgnoreCase(name, std::wstring(appbox::filesystem_marker::kOpaqueName)))
    {
        return true;
    }

    const auto suffix = std::wstring(appbox::filesystem_marker::kWhiteoutSuffix);
    if (name.size() <= suffix.size())
    {
        return false;
    }

    return EqualsIgnoreCase(name.substr(name.size() - suffix.size()), suffix);
}

/**
 * @brief Whether one startup file refers to one file of the view.
 * @param[in] file Startup file to test.
 * @param[in] preset_id Identifier of the preset directory.
 * @param[in] import_name Name of the imported folder.
 * @param[in] relative_path Path of the file relative to the import root.
 * @return true when the three references match ignoring case.
 */
bool SameStartupFile(const appbox::StartupFile& file, const std::string& preset_id, const std::wstring& import_name,
                     const std::wstring& relative_path)
{
    return file.preset_id == preset_id && EqualsIgnoreCase(file.import_name, import_name) &&
           EqualsIgnoreCase(file.relative_path, relative_path);
}

} // namespace

namespace appbox
{

std::wstring DefaultStartupTrigger(const std::wstring& relative_path)
{
    return std::filesystem::path(relative_path).stem().wstring();
}

std::wstring TrimStartupTrigger(const std::wstring& text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::iswspace(text[begin]) != 0)
    {
        ++begin;
    }
    while (end > begin && std::iswspace(text[end - 1]) != 0)
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::wstring FreeStartupTrigger(const std::vector<StartupFile>& files, const std::wstring& base)
{
    const auto taken = [&](const std::wstring& trigger) {
        return std::any_of(files.begin(), files.end(),
                           [&](const StartupFile& file) { return EqualsIgnoreCase(file.trigger, trigger); });
    };

    std::wstring candidate = base;
    for (int suffix = 2; taken(candidate); ++suffix)
    {
        candidate = base + L"-" + std::to_wstring(suffix);
    }
    return candidate;
}

void PackModel::Clear()
{
    imports_.clear();
    imported_files_.clear();
    directories_.clear();
    whiteouts_.clear();
    startup_files_.clear();
}

bool PackModel::IsEmpty() const
{
    return imports_.empty() && imported_files_.empty() && directories_.empty() && whiteouts_.empty() &&
           startup_files_.empty();
}

bool PackModel::ImportFolder(const std::string& preset_id, const std::wstring& source_path, std::string& error)
{
    PresetDirectory preset;
    if (!FindPresetDirectory(preset_id, preset))
    {
        error = "unknown preset directory: " + preset_id;
        return false;
    }

    const auto source = std::filesystem::path(source_path).lexically_normal();
    const auto name = source.filename().wstring();
    if (name.empty() || name == L"." || name == L"..")
    {
        error = "the source folder has no usable directory name";
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(source, ec))
    {
        error = "the source folder does not exist or is not a directory";
        return false;
    }

    for (const auto& imported : imports_)
    {
        if (imported.preset_id == preset_id && EqualsIgnoreCase(imported.import_name, name))
        {
            error = "'" + WideToUTF8(name) + "' already exists below " + WideToUTF8(preset.display_name);
            return false;
        }
    }

    ImportedFolder imported;
    imported.preset_id = preset_id;
    imported.import_name = name;
    imported.source_path = source.wstring();
    imports_.push_back(std::move(imported));
    return true;
}

void PackModel::RemoveImport(const std::string& preset_id, const std::wstring& import_name)
{
    for (auto it = imports_.begin(); it != imports_.end(); ++it)
    {
        if (it->preset_id == preset_id && EqualsIgnoreCase(it->import_name, import_name))
        {
            const auto removed = *it;
            imports_.erase(it);

            /* Imported files of the removed folder have no lower layer anymore. */
            imported_files_.erase(std::remove_if(imported_files_.begin(), imported_files_.end(),
                                                 [&removed](const ImportedFile& file) {
                                                     if (file.preset_id != removed.preset_id)
                                                     {
                                                         return false;
                                                     }
                                                     const auto segments = Split(file.target_dir, L"\\");
                                                     return !segments.empty() &&
                                                            EqualsIgnoreCase(segments.front(), removed.import_name);
                                                 }),
                                  imported_files_.end());

            /*
             * Folders the user created below the removed import have no layer
             * of the view anymore, so they go with it.
             */
            directories_.erase(std::remove_if(directories_.begin(), directories_.end(),
                                              [&removed](const VirtualDirectory& directory) {
                                                  if (directory.preset_id != removed.preset_id)
                                                  {
                                                      return false;
                                                  }
                                                  const auto segments = Split(directory.target_dir, L"\\");
                                                  return !segments.empty() &&
                                                         EqualsIgnoreCase(segments.front(), removed.import_name);
                                              }),
                               directories_.end());

            /*
             * An entry the removed folder hid has no parent of the view
             * anymore, so the marker goes with the folder which carried it.
             */
            whiteouts_.erase(std::remove_if(whiteouts_.begin(), whiteouts_.end(),
                                            [&removed](const WhiteoutEntry& entry) {
                                                if (entry.preset_id != removed.preset_id)
                                                {
                                                    return false;
                                                }
                                                const auto segments = Split(entry.target_dir, L"\\");
                                                return !segments.empty() &&
                                                       EqualsIgnoreCase(segments.front(), removed.import_name);
                                            }),
                             whiteouts_.end());

            /* Startup files of the removed folder have no lower layer anymore. */
            startup_files_.erase(std::remove_if(startup_files_.begin(), startup_files_.end(),
                                                [&removed](const StartupFile& file) {
                                                    return file.preset_id == removed.preset_id &&
                                                           EqualsIgnoreCase(file.import_name, removed.import_name);
                                                }),
                                 startup_files_.end());
            return;
        }
    }
}

bool PackModel::ImportFiles(const std::string& preset_id, const std::wstring& target_dir,
                            const std::vector<std::wstring>& source_paths, std::string& error)
{
    PresetDirectory preset;
    if (!FindPresetDirectory(preset_id, preset))
    {
        error = "unknown preset directory: " + preset_id;
        return false;
    }

    /*
     * The target directory is a folder of the view and may be the layer root of
     * the preset directory, and the first segment is stored in the spelling the
     * model carries it in, so a file which was imported below an imported
     * folder keeps the name of that import.
     */
    const auto directory = NormalizeRelativeDir(target_dir);
    if (!target_dir.empty() && directory.empty())
    {
        error = "the target directory is not a valid relative path";
        return false;
    }

    const auto canonical_dir = CanonicalDirectoryPath(preset_id, directory);

    if (source_paths.empty())
    {
        error = "no source file was selected";
        return false;
    }

    std::vector<ImportedFile> pending;
    pending.reserve(source_paths.size());

    for (const auto& source_path : source_paths)
    {
        const auto source = std::filesystem::path(source_path).lexically_normal();
        const auto name = source.filename().wstring();
        if (name.empty() || name == L"." || name == L"..")
        {
            error = "the source file has no usable name: " + WideToUTF8(source_path);
            return false;
        }

        std::error_code ec;
        if (!std::filesystem::is_regular_file(source, ec))
        {
            error = "the source file does not exist: " + WideToUTF8(source_path);
            return false;
        }

        error = CheckImportedFilePlacement(preset_id, canonical_dir, name);
        if (!error.empty())
        {
            return false;
        }

        const auto duplicated = std::any_of(pending.begin(), pending.end(), [&](const ImportedFile& file) {
            return file.preset_id == preset_id && EqualsIgnoreCase(file.target_dir, canonical_dir) &&
                   EqualsIgnoreCase(file.file_name, name);
        });
        if (duplicated)
        {
            error = "'" + WideToUTF8(name) + "' was selected twice";
            return false;
        }

        /*
         * A name which the host content of an imported folder holds already
         * would produce a duplicate entry inside the archive.
         */
        const auto path = canonical_dir.empty() ? name : canonical_dir + L"\\" + name;
        if (HostImportCarries(preset_id, path))
        {
            error = "'" + WideToUTF8(name) + "' already exists in the imported folder";
            return false;
        }

        ImportedFile file;
        file.preset_id = preset_id;
        file.target_dir = canonical_dir;
        file.file_name = name;
        file.source_path = source.wstring();
        pending.push_back(std::move(file));
    }

    imported_files_.insert(imported_files_.end(), pending.begin(), pending.end());
    return true;
}

bool PackModel::RemoveImportedFile(const std::string& preset_id, const std::wstring& target_dir,
                                   const std::wstring& file_name)
{
    const auto directory = NormalizeRelativeDir(target_dir);

    for (auto it = imported_files_.begin(); it != imported_files_.end(); ++it)
    {
        if (it->preset_id == preset_id && EqualsIgnoreCase(it->target_dir, directory) &&
            EqualsIgnoreCase(it->file_name, file_name))
        {
            /*
             * The startup file pointing at the removed file refers to an entry
             * which is no longer part of the archive.
             */
            const auto segments = Split(it->target_dir, L"\\");
            const auto remainder = JoinSegments(segments, 1);
            const auto relative = remainder.empty() ? it->file_name : remainder + L"\\" + it->file_name;
            const auto import_name = segments.empty() ? std::wstring() : segments.front();

            imported_files_.erase(it);
            startup_files_.erase(std::remove_if(startup_files_.begin(), startup_files_.end(),
                                                [&](const StartupFile& file) {
                                                    return SameStartupFile(file, preset_id, import_name, relative);
                                                }),
                                 startup_files_.end());
            return true;
        }
    }
    return false;
}

std::wstring PackModel::FreeVirtualDirectoryName(const std::string& preset_id, const std::wstring& parent_dir) const
{
    const auto parent = NormalizeRelativeDir(parent_dir);

    const std::wstring base = L"New Folder";
    std::wstring       candidate = base;
    for (int suffix = 2; !CheckVirtualDirectoryName(preset_id, parent, candidate).empty(); ++suffix)
    {
        candidate = base + L" " + std::to_wstring(suffix);
    }
    return candidate;
}

bool PackModel::CreateVirtualDirectory(const std::string& preset_id, const std::wstring& parent_dir,
                                       const std::wstring& name, std::string& error)
{
    PresetDirectory preset;
    if (!FindPresetDirectory(preset_id, preset))
    {
        error = "unknown preset directory: " + preset_id;
        return false;
    }

    const auto parent = NormalizeRelativeDir(parent_dir);
    if (!parent_dir.empty() && parent.empty())
    {
        error = "the parent directory is not a valid relative path";
        return false;
    }

    if (!IsPlainName(name))
    {
        error = "'" + WideToUTF8(name) + "' is not a usable folder name";
        return false;
    }

    if (!DirectoryParentExists(preset_id, parent, true))
    {
        error = "'" + WideToUTF8(parent) + "' is not a folder of the view";
        return false;
    }

    error = CheckVirtualDirectoryName(preset_id, parent, name);
    if (!error.empty())
    {
        return false;
    }

    VirtualDirectory directory;
    directory.preset_id = preset_id;
    directory.target_dir = CanonicalDirectoryPath(preset_id, parent.empty() ? name : parent + L"\\" + name);
    directories_.push_back(std::move(directory));
    return true;
}

bool PackModel::RenameVirtualDirectory(const std::string& preset_id, const std::wstring& target_dir,
                                       const std::wstring& new_name, std::string& error)
{
    PresetDirectory preset;
    if (!FindPresetDirectory(preset_id, preset))
    {
        error = "unknown preset directory: " + preset_id;
        return false;
    }

    const auto path = NormalizeRelativeDir(target_dir);
    if (path.empty() || !HasVirtualDirectory(preset_id, path))
    {
        error = "'" + WideToUTF8(target_dir) + "' is not a folder of the view";
        return false;
    }

    if (!IsPlainName(new_name))
    {
        error = "'" + WideToUTF8(new_name) + "' is not a usable folder name";
        return false;
    }

    if (EqualsIgnoreCase(LeafName(path), new_name))
    {
        /* The folder keeps the name it carries, so its subtree stays as it is. */
        return true;
    }

    const auto parent = ParentDirectory(path);
    error = CheckVirtualDirectoryName(preset_id, parent, new_name);
    if (!error.empty())
    {
        return false;
    }

    /*
     * The folder and every folder below it are stored under the new path, so
     * the subtree keeps its shape.
     */
    const auto renamed = parent.empty() ? new_name : parent + L"\\" + new_name;
    for (auto& directory : directories_)
    {
        if (directory.preset_id != preset_id)
        {
            continue;
        }
        if (EqualsIgnoreCase(directory.target_dir, path))
        {
            directory.target_dir = renamed;
        }
        else if (IsPathBelow(directory.target_dir, path))
        {
            directory.target_dir = renamed + directory.target_dir.substr(path.size());
        }
    }
    return true;
}

bool PackModel::RemoveVirtualDirectory(const std::string& preset_id, const std::wstring& target_dir)
{
    const auto path = NormalizeRelativeDir(target_dir);
    if (path.empty() || !HasVirtualDirectory(preset_id, path))
    {
        return false;
    }

    /*
     * An imported file below the folder has no directory of the view anymore,
     * so it is removed the way RemoveImportedFile() removes one, which drops
     * the startup file pointing at it as well.
     */
    std::vector<ImportedFile> orphans;
    for (const auto& file : imported_files_)
    {
        if (file.preset_id == preset_id &&
            (EqualsIgnoreCase(file.target_dir, path) || IsPathBelow(file.target_dir, path)))
        {
            orphans.push_back(file);
        }
    }
    for (const auto& file : orphans)
    {
        RemoveImportedFile(preset_id, file.target_dir, file.file_name);
    }

    directories_.erase(std::remove_if(directories_.begin(), directories_.end(),
                                      [&](const VirtualDirectory& directory) {
                                          return directory.preset_id == preset_id &&
                                                 (EqualsIgnoreCase(directory.target_dir, path) ||
                                                  IsPathBelow(directory.target_dir, path));
                                      }),
                       directories_.end());

    /* The entries the removed folder hid have no parent of the view anymore. */
    RemoveWhiteoutsUnder(preset_id, path);
    return true;
}

std::vector<VirtualDirectory> PackModel::VirtualDirectoriesOf(const std::string&  preset_id,
                                                              const std::wstring& target_dir) const
{
    const auto parent = NormalizeRelativeDir(target_dir);

    std::vector<VirtualDirectory> result;
    for (const auto& directory : directories_)
    {
        if (directory.preset_id == preset_id && EqualsIgnoreCase(ParentDirectory(directory.target_dir), parent))
        {
            result.push_back(directory);
        }
    }
    return result;
}

const std::vector<VirtualDirectory>& PackModel::AllVirtualDirectories() const
{
    return directories_;
}

bool PackModel::HasVirtualDirectory(const std::string& preset_id, const std::wstring& target_dir) const
{
    const auto path = NormalizeRelativeDir(target_dir);
    if (path.empty())
    {
        /* The layer root of a preset directory is not a folder of the model. */
        return false;
    }

    return std::any_of(directories_.begin(), directories_.end(), [&](const VirtualDirectory& directory) {
        return directory.preset_id == preset_id && EqualsIgnoreCase(directory.target_dir, path);
    });
}

bool PackModel::RestoreVirtualDirectory(const std::string& preset_id, const std::wstring& target_dir,
                                        std::string& error)
{
    PresetDirectory preset;
    if (!FindPresetDirectory(preset_id, preset))
    {
        error = "unknown preset directory: " + preset_id;
        return false;
    }

    const auto path = NormalizeRelativeDir(target_dir);
    if (path.empty())
    {
        error = "the folder path is empty or leaves the preset directory";
        return false;
    }

    /*
     * The parent is checked without the host: a project can be imported on a
     * machine where the folder of the import does not exist, and the document
     * lists the folders from the top down, so the parent is part of the model
     * already.
     */
    const auto parent = ParentDirectory(path);
    if (!DirectoryParentExists(preset_id, parent, false))
    {
        error = "'" + WideToUTF8(parent) + "' is not a folder of the view";
        return false;
    }

    if (HasVirtualDirectory(preset_id, path))
    {
        error = "'" + WideToUTF8(path) + "' is listed twice";
        return false;
    }

    ImportedFolder imported;
    if (GetImport(preset_id, path, imported))
    {
        error = "'" + WideToUTF8(path) + "' is an imported folder";
        return false;
    }

    if (HasImportedFileAt(preset_id, path))
    {
        error = "'" + WideToUTF8(path) + "' is an imported file";
        return false;
    }

    VirtualDirectory directory;
    directory.preset_id = preset_id;
    directory.target_dir = CanonicalDirectoryPath(preset_id, path);
    directories_.push_back(std::move(directory));
    return true;
}

bool PackModel::AddWhiteout(const std::string& preset_id, const std::wstring& target_dir, const std::wstring& name,
                            std::string& error)
{
    PresetDirectory preset;
    if (!FindPresetDirectory(preset_id, preset))
    {
        error = "unknown preset directory: " + preset_id;
        return false;
    }

    const auto directory = NormalizeRelativeDir(target_dir);
    if (!target_dir.empty() && directory.empty())
    {
        error = "the target directory is not a valid relative path";
        return false;
    }

    if (!IsPlainName(name))
    {
        error = "'" + WideToUTF8(name) + "' is not a usable name";
        return false;
    }

    /*
     * The marker of an entry is a sibling of the entry, so a name the view
     * reserves for its markers names no entry which could be hidden.
     */
    if (IsMarkerName(name))
    {
        error = "'" + WideToUTF8(name) + "' is a name the sandbox view reserves";
        return false;
    }

    if (HasWhiteout(preset_id, directory.empty() ? name : directory + L"\\" + name))
    {
        error = "'" + WideToUTF8(name) + "' is hidden already";
        return false;
    }

    WhiteoutEntry entry;
    entry.preset_id = preset_id;
    entry.target_dir = directory;
    entry.name = name;
    whiteouts_.push_back(std::move(entry));
    return true;
}

bool PackModel::RemoveWhiteout(const std::string& preset_id, const std::wstring& target_dir, const std::wstring& name)
{
    const auto directory = NormalizeRelativeDir(target_dir);

    for (auto it = whiteouts_.begin(); it != whiteouts_.end(); ++it)
    {
        if (it->preset_id == preset_id && EqualsIgnoreCase(it->target_dir, directory) &&
            EqualsIgnoreCase(it->name, name))
        {
            whiteouts_.erase(it);
            return true;
        }
    }

    return false;
}

bool PackModel::RemoveWhiteoutsUnder(const std::string& preset_id, const std::wstring& target_dir)
{
    const auto directory = NormalizeRelativeDir(target_dir);

    /*
     * An empty directory names the layer root, which holds every entry of the
     * preset directory, so all of them are dropped in that case.
     */
    const bool every_entry = directory.empty();

    const auto before = whiteouts_.size();
    whiteouts_.erase(std::remove_if(whiteouts_.begin(), whiteouts_.end(),
                                    [&](const WhiteoutEntry& entry) {
                                        if (entry.preset_id != preset_id)
                                        {
                                            return false;
                                        }
                                        return every_entry || EqualsIgnoreCase(entry.target_dir, directory) ||
                                               IsPathBelow(entry.target_dir, directory);
                                    }),
                     whiteouts_.end());

    return whiteouts_.size() != before;
}

std::vector<WhiteoutEntry> PackModel::WhiteoutsOf(const std::string& preset_id, const std::wstring& target_dir) const
{
    const auto directory = NormalizeRelativeDir(target_dir);

    std::vector<WhiteoutEntry> result;
    for (const auto& entry : whiteouts_)
    {
        if (entry.preset_id == preset_id && EqualsIgnoreCase(entry.target_dir, directory))
        {
            result.push_back(entry);
        }
    }
    return result;
}

const std::vector<WhiteoutEntry>& PackModel::AllWhiteouts() const
{
    return whiteouts_;
}

bool PackModel::HasWhiteout(const std::string& preset_id, const std::wstring& path) const
{
    const auto normalized = NormalizeRelativeDir(path);
    const auto directory = ParentDirectory(normalized);
    const auto name = LeafName(normalized);

    return std::any_of(whiteouts_.begin(), whiteouts_.end(), [&](const WhiteoutEntry& entry) {
        return entry.preset_id == preset_id && EqualsIgnoreCase(entry.target_dir, directory) &&
               EqualsIgnoreCase(entry.name, name);
    });
}

bool PackModel::RestoreWhiteout(const std::string& preset_id, const std::wstring& target_dir, const std::wstring& name,
                                std::string& error)
{
    /*
     * A record of a document names an entry of the host which does not have to
     * exist on the machine which reads the document, so the shape of the
     * record is the only thing which is validated; that is what AddWhiteout()
     * checks as well.
     */
    return AddWhiteout(preset_id, target_dir, name, error);
}

bool PackModel::IsDirectoryOfView(const std::string& preset_id, const std::wstring& target_dir) const
{
    PresetDirectory preset;
    if (!FindPresetDirectory(preset_id, preset))
    {
        return false;
    }

    const auto path = NormalizeRelativeDir(target_dir);
    if (!target_dir.empty() && path.empty())
    {
        /* A path which leaves the preset directory is not a folder of the view. */
        return false;
    }

    /*
     * The rule is the one which decides whether a folder can hold an entry:
     * the layer root, a folder the user created, an import root and a folder
     * below an import are folders of the view.
     */
    return DirectoryParentExists(preset_id, path, false);
}

std::vector<ImportedFile> PackModel::FilesOf(const std::string& preset_id, const std::wstring& target_dir) const
{
    const auto directory = NormalizeRelativeDir(target_dir);

    std::vector<ImportedFile> result;
    for (const auto& file : imported_files_)
    {
        if (file.preset_id == preset_id && EqualsIgnoreCase(file.target_dir, directory))
        {
            result.push_back(file);
        }
    }
    return result;
}

const std::vector<ImportedFile>& PackModel::AllImportedFiles() const
{
    return imported_files_;
}

bool PackModel::ResolveStartupFile(const std::string& preset_id, const std::wstring& import_name,
                                   const std::wstring& relative_path, bool require_host, StartupFile& out,
                                   std::string& error) const
{
    PresetDirectory preset;
    if (!FindPresetDirectory(preset_id, preset))
    {
        error = "unknown preset directory: " + preset_id;
        return false;
    }

    const auto relative = NormalizeRelativeFile(relative_path);
    if (relative.empty())
    {
        error = "the startup file path is empty or leaves its preset directory";
        return false;
    }

    /*
     * Only the files the launcher knows how to start can be a startup file: a
     * program on its own, a batch file and a command script through the command
     * interpreter and a PowerShell script through Windows PowerShell.
     */
    if (!IsStartableFile(relative))
    {
        error = "the startup file '" + WideToUTF8(relative) + "' is not a startable file (" +
                StartupFileExtensionList() + ")";
        return false;
    }

    /*
     * The file is named by the path it carries inside the layer of its preset
     * directory: the first segment names the lower layer it extends, which is
     * an imported folder or a folder the user created, and a file which lives
     * in the layer root is named by its own name alone.
     */
    const auto segments = Split(import_name, L"\\");
    const auto root = segments.empty() ? std::wstring() : CanonicalDirectoryPath(preset_id, segments.front());
    const auto view_path = root.empty() ? relative : root + L"\\" + relative;

    const auto directory = ParentDirectory(view_path);
    const auto name = LeafName(view_path);
    if (name.empty() || !IsDirectoryOfView(preset_id, directory))
    {
        error = "the startup file '" + WideToUTF8(view_path) + "' is not a file of the view";
        return false;
    }

    if (require_host)
    {
        /*
         * A file which was imported on its own lives in the sandbox view and
         * not in the host folder of its directory, so it counts as it is; every
         * other file is looked up in the host folder of the view path.
         */
        bool exists = HasImportedFileAt(preset_id, view_path);
        if (!exists)
        {
            std::wstring host;
            if (HostFolderOf(preset_id, directory, host) && !host.empty())
            {
                std::error_code ec;
                exists = std::filesystem::is_regular_file(std::filesystem::path(host) / name, ec) && !ec;
            }
        }

        if (!exists)
        {
            error = "the startup file does not exist: " + WideToUTF8(view_path);
            return false;
        }
    }

    /*
     * The file is stored the way the launcher reads it: the first segment of
     * the path inside the layer and the rest of that path.
     */
    const auto parts = Split(view_path, L"\\");
    if (parts.size() > 1)
    {
        out.import_name = parts.front();
        out.relative_path = JoinSegments(parts, 1);
    }
    else
    {
        out.import_name.clear();
        out.relative_path = view_path;
    }

    out.preset_id = preset_id;
    return true;
}

const std::vector<StartupFile>& PackModel::StartupFiles() const
{
    return startup_files_;
}

bool PackModel::HasStartupFiles() const
{
    return !startup_files_.empty();
}

bool PackModel::HasAutoStart() const
{
    return std::any_of(startup_files_.begin(), startup_files_.end(),
                       [](const StartupFile& file) { return file.auto_start; });
}

bool PackModel::IsStartupFile(const std::string& preset_id, const std::wstring& import_name,
                              const std::wstring& relative_path) const
{
    const auto relative = NormalizeRelativeFile(relative_path);
    return std::any_of(startup_files_.begin(), startup_files_.end(), [&](const StartupFile& file) {
        return SameStartupFile(file, preset_id, import_name, relative);
    });
}

bool PackModel::AddStartupFile(const std::string& preset_id, const std::wstring& import_name,
                               const std::wstring& relative_path, bool auto_start, std::string& error)
{
    StartupFile resolved;
    if (!ResolveStartupFile(preset_id, import_name, relative_path, true, resolved, error))
    {
        return false;
    }

    for (auto& file : startup_files_)
    {
        if (SameStartupFile(file, resolved.preset_id, resolved.import_name, resolved.relative_path))
        {
            /* The file is in the list already: only the flag changes. */
            file.auto_start = auto_start;
            return true;
        }
    }

    resolved.trigger = FreeStartupTrigger(startup_files_, DefaultStartupTrigger(resolved.relative_path));
    resolved.auto_start = auto_start;
    startup_files_.push_back(std::move(resolved));
    return true;
}

bool PackModel::SetStartupFiles(std::vector<StartupFile> files, std::string& error)
{
    std::vector<StartupFile> candidate;
    candidate.reserve(files.size());

    for (std::size_t i = 0; i < files.size(); ++i)
    {
        StartupFile resolved;
        if (!ResolveStartupFile(files[i].preset_id, files[i].import_name, files[i].relative_path, true, resolved,
                                error))
        {
            return false;
        }

        resolved.trigger = TrimStartupTrigger(files[i].trigger);
        if (resolved.trigger.empty())
        {
            error = "the startup file trigger is empty";
            return false;
        }

        const auto same_path = std::any_of(candidate.begin(), candidate.end(), [&](const StartupFile& other) {
            return SameStartupFile(other, resolved.preset_id, resolved.import_name, resolved.relative_path);
        });
        if (same_path)
        {
            error = "the startup file '" + WideToUTF8(resolved.relative_path) + "' is listed twice";
            return false;
        }

        const auto same_trigger = std::any_of(candidate.begin(), candidate.end(), [&](const StartupFile& other) {
            return EqualsIgnoreCase(other.trigger, resolved.trigger);
        });
        if (same_trigger)
        {
            error = "the startup file trigger '" + WideToUTF8(resolved.trigger) + "' is used twice";
            return false;
        }

        resolved.auto_start = files[i].auto_start;
        candidate.push_back(std::move(resolved));
    }

    startup_files_ = std::move(candidate);
    return true;
}

bool PackModel::RestoreImportedFolder(const std::string& preset_id, const std::wstring& import_name,
                                      const std::wstring& source_path, std::string& error)
{
    PresetDirectory preset;
    if (!FindPresetDirectory(preset_id, preset))
    {
        error = "unknown preset directory: " + preset_id;
        return false;
    }

    if (!IsPlainName(import_name))
    {
        error = "'" + WideToUTF8(import_name) + "' is not a usable folder name";
        return false;
    }

    if (source_path.empty())
    {
        error = "'" + WideToUTF8(import_name) + "' has no source folder";
        return false;
    }

    for (const auto& imported : imports_)
    {
        if (imported.preset_id == preset_id && EqualsIgnoreCase(imported.import_name, import_name))
        {
            error = "'" + WideToUTF8(import_name) + "' already exists below " + WideToUTF8(preset.display_name);
            return false;
        }
    }

    ImportedFolder imported;
    imported.preset_id = preset_id;
    imported.import_name = import_name;
    imported.source_path = source_path;
    imports_.push_back(std::move(imported));
    return true;
}

bool PackModel::RestoreImportedFile(const std::string& preset_id, const std::wstring& target_dir,
                                    const std::wstring& file_name, const std::wstring& source_path, std::string& error)
{
    PresetDirectory preset;
    if (!FindPresetDirectory(preset_id, preset))
    {
        error = "unknown preset directory: " + preset_id;
        return false;
    }

    if (!IsPlainName(file_name))
    {
        error = "'" + WideToUTF8(file_name) + "' is not a usable file name";
        return false;
    }

    const auto directory = NormalizeRelativeDir(target_dir);
    if (!target_dir.empty() && directory.empty())
    {
        error = "the target directory is not a valid relative path";
        return false;
    }

    error = CheckImportedFilePlacement(preset_id, directory, file_name);
    if (!error.empty())
    {
        return false;
    }

    if (source_path.empty())
    {
        error = "'" + WideToUTF8(file_name) + "' has no source file";
        return false;
    }

    ImportedFile file;
    file.preset_id = preset_id;
    file.target_dir = CanonicalDirectoryPath(preset_id, directory);
    file.file_name = file_name;
    file.source_path = source_path;
    imported_files_.push_back(std::move(file));
    return true;
}

bool PackModel::RestoreStartupFile(const std::string& preset_id, const std::wstring& import_name,
                                   const std::wstring& relative_path, const std::wstring& trigger, bool auto_start,
                                   std::string& error)
{
    StartupFile resolved;
    if (!ResolveStartupFile(preset_id, import_name, relative_path, false, resolved, error))
    {
        return false;
    }

    resolved.trigger = TrimStartupTrigger(trigger);
    if (resolved.trigger.empty())
    {
        error = "the startup file trigger is empty";
        return false;
    }

    const auto same_path = std::any_of(startup_files_.begin(), startup_files_.end(), [&](const StartupFile& file) {
        return SameStartupFile(file, resolved.preset_id, resolved.import_name, resolved.relative_path);
    });
    if (same_path)
    {
        error = "the startup file '" + WideToUTF8(resolved.relative_path) + "' is listed twice";
        return false;
    }

    const auto same_trigger = std::any_of(startup_files_.begin(), startup_files_.end(), [&](const StartupFile& file) {
        return EqualsIgnoreCase(file.trigger, resolved.trigger);
    });
    if (same_trigger)
    {
        error = "the startup file trigger '" + WideToUTF8(resolved.trigger) + "' is used twice";
        return false;
    }

    resolved.auto_start = auto_start;
    startup_files_.push_back(std::move(resolved));
    return true;
}

std::vector<ImportedFolder> PackModel::ImportsOf(const std::string& preset_id) const
{
    std::vector<ImportedFolder> result;
    for (const auto& imported : imports_)
    {
        if (imported.preset_id == preset_id)
        {
            result.push_back(imported);
        }
    }
    return result;
}

bool PackModel::GetImport(const std::string& preset_id, const std::wstring& import_name, ImportedFolder& out) const
{
    for (const auto& imported : imports_)
    {
        if (imported.preset_id == preset_id && EqualsIgnoreCase(imported.import_name, import_name))
        {
            out = imported;
            return true;
        }
    }
    return false;
}

bool PackModel::StartupFilePath(const StartupFile& file, std::wstring& path) const
{
    const auto view_path =
        file.import_name.empty() ? file.relative_path : file.import_name + L"\\" + file.relative_path;
    const auto directory = ParentDirectory(view_path);
    const auto name = LeafName(view_path);

    /*
     * A file which was imported on its own names the host file it was imported
     * from, which is the file the run reads and the one a caller asks for.
     */
    for (const auto& imported : imported_files_)
    {
        if (imported.preset_id == file.preset_id && EqualsIgnoreCase(imported.target_dir, directory) &&
            EqualsIgnoreCase(imported.file_name, name))
        {
            path = imported.source_path;
            return true;
        }
    }

    std::wstring host;
    if (!HostFolderOf(file.preset_id, directory, host) || host.empty())
    {
        return false;
    }

    path = (std::filesystem::path(host) / name).wstring();
    return true;
}

bool PackModel::HostFolderPath(const std::string& preset_id, const std::wstring& import_name,
                               const std::wstring& relative_dir, std::wstring& path) const
{
    /*
     * The container of the filesystem view holds the preset directories and is
     * not a folder of the model, so it has no host counterpart.
     */
    if (preset_id.empty())
    {
        return false;
    }

    if (import_name.empty())
    {
        /* A preset directory is the layer root, which is a real host folder. */
        PresetDirectory preset;
        if (!FindPresetDirectory(preset_id, preset) || preset.real_path.empty())
        {
            return false;
        }

        path = preset.real_path;
        return true;
    }

    ImportedFolder imported;
    if (!GetImport(preset_id, import_name, imported))
    {
        return false;
    }

    path = relative_dir.empty() ? imported.source_path
                                : (std::filesystem::path(imported.source_path) / relative_dir).wstring();
    return true;
}

bool PackModel::DirectoryParentExists(const std::string& preset_id, const std::wstring& parent_dir,
                                      bool require_host) const
{
    /* The layer root of a preset directory is always a folder of the view. */
    if (parent_dir.empty())
    {
        return true;
    }

    if (HasVirtualDirectory(preset_id, parent_dir))
    {
        return true;
    }

    const auto     segments = Split(parent_dir, L"\\");
    ImportedFolder imported;
    if (segments.empty() || !GetImport(preset_id, segments.front(), imported))
    {
        return false;
    }

    const auto remainder = JoinSegments(segments, 1);
    if (remainder.empty() || !require_host)
    {
        return true;
    }

    std::error_code ec;
    const auto      folder = std::filesystem::path(imported.source_path) / remainder;
    return std::filesystem::is_directory(folder, ec) && !ec;
}

bool PackModel::HostFolderOf(const std::string& preset_id, const std::wstring& target_dir, std::wstring& path) const
{
    /*
     * A folder the user created is an entry of the layer alone, so it maps to
     * no folder of the host.
     */
    if (HasVirtualDirectory(preset_id, target_dir))
    {
        return false;
    }

    if (target_dir.empty())
    {
        PresetDirectory preset;
        if (!FindPresetDirectory(preset_id, preset) || preset.real_path.empty())
        {
            return false;
        }

        path = preset.real_path;
        return true;
    }

    const auto     segments = Split(target_dir, L"\\");
    ImportedFolder imported;
    if (segments.empty() || !GetImport(preset_id, segments.front(), imported))
    {
        return false;
    }

    const auto remainder = JoinSegments(segments, 1);
    path =
        remainder.empty() ? imported.source_path : (std::filesystem::path(imported.source_path) / remainder).wstring();
    return true;
}

std::string PackModel::CheckVirtualDirectoryName(const std::string& preset_id, const std::wstring& parent_dir,
                                                 const std::wstring& name) const
{
    if (!IsPlainName(name))
    {
        return "'" + WideToUTF8(name) + "' is not a usable folder name";
    }

    const auto path = parent_dir.empty() ? name : parent_dir + L"\\" + name;

    if (HasVirtualDirectory(preset_id, path))
    {
        return "'" + WideToUTF8(name) + "' is already a folder of the view";
    }

    ImportedFolder imported;
    if (GetImport(preset_id, path, imported))
    {
        return "'" + WideToUTF8(name) + "' is an imported folder";
    }

    /*
     * A folder and a file of the same path would be two entries of one name
     * inside the archive, which the zip writer refuses.
     */
    if (HasImportedFileAt(preset_id, path))
    {
        return "'" + WideToUTF8(name) + "' is an imported file";
    }

    /*
     * A folder of the host of the same name would show up twice in the view of
     * the directory, because the layer is merged with the host at run time. The
     * host is only read, so a name stays usable while the imported folder is
     * missing on this machine.
     */
    std::wstring host_parent;
    if (HostFolderOf(preset_id, parent_dir, host_parent))
    {
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(host_parent) / name, ec) && !ec)
        {
            return "'" + WideToUTF8(name) + "' already exists in the host folder of the view";
        }
    }

    return {};
}

bool PackModel::HasImportedFileAt(const std::string& preset_id, const std::wstring& path) const
{
    const auto directory = ParentDirectory(path);
    const auto name = LeafName(path);
    return std::any_of(imported_files_.begin(), imported_files_.end(), [&](const ImportedFile& file) {
        return file.preset_id == preset_id && EqualsIgnoreCase(file.target_dir, directory) &&
               EqualsIgnoreCase(file.file_name, name);
    });
}

std::string PackModel::CheckImportedFilePlacement(const std::string& preset_id, const std::wstring& directory,
                                                  const std::wstring& file_name) const
{
    PresetDirectory preset;
    if (!FindPresetDirectory(preset_id, preset))
    {
        return "unknown preset directory: " + preset_id;
    }

    if (!IsPlainName(file_name))
    {
        return "'" + WideToUTF8(file_name) + "' is not a usable file name";
    }

    /*
     * The file is placed in a folder of the view, and the layer root of the
     * preset directory is one of them: a capture fills its entries at the very
     * paths the host holds them at, and a file of the root of a preset
     * directory has no folder of its own to live in.
     */
    if (!IsDirectoryOfView(preset_id, directory))
    {
        return "'" + WideToUTF8(directory) + "' is not a folder of the view";
    }

    const auto path = directory.empty() ? file_name : directory + L"\\" + file_name;

    if (HasVirtualDirectory(preset_id, path))
    {
        return "'" + WideToUTF8(file_name) + "' is a folder of the view";
    }

    if (HasImportedFileAt(preset_id, path))
    {
        return "'" + WideToUTF8(file_name) + "' was already imported into " + WideToUTF8(directory);
    }

    return {};
}

bool PackModel::HostImportCarries(const std::string& preset_id, const std::wstring& path) const
{
    const auto normalized = NormalizeRelativeDir(path);
    const auto directory = ParentDirectory(normalized);
    if (directory.empty())
    {
        /*
         * The layer root of a preset directory is not an imported folder: the
         * content of the preset folder itself does not travel into the archive.
         */
        return false;
    }

    std::wstring host;
    if (!HostFolderOf(preset_id, directory, host) || host.empty())
    {
        return false;
    }

    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(host) / LeafName(normalized), ec) && !ec;
}

std::wstring PackModel::CanonicalDirectoryPath(const std::string& preset_id, const std::wstring& path) const
{
    const auto     segments = Split(path, L"\\");
    ImportedFolder imported;
    if (segments.empty() || !GetImport(preset_id, segments.front(), imported))
    {
        return path;
    }

    const auto remainder = JoinSegments(segments, 1);
    return remainder.empty() ? imported.import_name : imported.import_name + L"\\" + remainder;
}

} // namespace appbox
