#include "PackModel.hpp"
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

} // namespace

namespace appbox
{

void PackModel::Clear()
{
    imports_.clear();
    imported_files_.clear();
    main_program_ = MainProgram{};
    has_main_program_ = false;
}

bool PackModel::IsEmpty() const
{
    return imports_.empty() && imported_files_.empty() && !has_main_program_;
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
            imported_files_.erase(
                std::remove_if(imported_files_.begin(), imported_files_.end(),
                               [&removed](const ImportedFile& file) {
                                   if (file.preset_id != removed.preset_id)
                                   {
                                       return false;
                                   }
                                   const auto segments = Split(file.target_dir, L"\\");
                                   return !segments.empty()
                                          && EqualsIgnoreCase(segments.front(), removed.import_name);
                               }),
                imported_files_.end());

            if (has_main_program_ && main_program_.preset_id == removed.preset_id
                && EqualsIgnoreCase(main_program_.import_name, removed.import_name))
            {
                has_main_program_ = false;
                main_program_ = MainProgram{};
            }
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

    const auto directory = NormalizeRelativeDir(target_dir);
    if (directory.empty())
    {
        error = "the target directory is not a valid relative path";
        return false;
    }

    /*
     * The first segment has to be an imported folder: the file extends an
     * existing lower layer instead of creating a new one.
     */
    const auto segments = Split(directory, L"\\");
    ImportedFolder imported;
    if (!GetImport(preset_id, segments.front(), imported))
    {
        error = "'" + WideToUTF8(segments.front()) + "' is not an imported folder";
        return false;
    }

    /* Store the canonical spelling of the imported folder. */
    std::wstring canonical_dir = imported.import_name;
    const auto remainder = JoinSegments(segments, 1);
    if (!remainder.empty())
    {
        canonical_dir.push_back(L'\\');
        canonical_dir += remainder;
    }

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

        const auto duplicated = [&](const std::vector<ImportedFile>& files) {
            return std::any_of(files.begin(), files.end(), [&](const ImportedFile& file) {
                return file.preset_id == preset_id && EqualsIgnoreCase(file.target_dir, canonical_dir)
                       && EqualsIgnoreCase(file.file_name, name);
            });
        };

        if (duplicated(pending) || duplicated(imported_files_))
        {
            error = "'" + WideToUTF8(name) + "' was already imported into "
                    + WideToUTF8(canonical_dir);
            return false;
        }

        /*
         * A name which already exists in the imported folder would produce a
         * duplicate entry inside the archive.
         */
        const auto host_path =
            std::filesystem::path(imported.source_path) / remainder / std::filesystem::path(name);
        if (std::filesystem::exists(host_path, ec))
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
        if (it->preset_id == preset_id && EqualsIgnoreCase(it->target_dir, directory)
            && EqualsIgnoreCase(it->file_name, file_name))
        {
            imported_files_.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<ImportedFile> PackModel::FilesOf(const std::string& preset_id,
                                             const std::wstring& target_dir) const
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

bool PackModel::SetMainProgram(const std::string& preset_id, const std::wstring& import_name,
                                const std::wstring& relative_path, std::string& error)
{
    PresetDirectory preset;
    if (!FindPresetDirectory(preset_id, preset))
    {
        error = "unknown preset directory: " + preset_id;
        return false;
    }

    ImportedFolder imported;
    if (!GetImport(preset_id, import_name, imported))
    {
        error = "the imported folder no longer exists";
        return false;
    }

    /* Normalize the separators so the loader side receives DOS style paths. */
    std::wstring relative = relative_path;
    for (auto& ch : relative)
    {
        if (ch == L'/')
        {
            ch = L'\\';
        }
    }
    while (!relative.empty() && relative.front() == L'\\')
    {
        relative.erase(relative.begin());
    }

    if (relative.empty())
    {
        error = "the main program path is empty";
        return false;
    }
    for (const auto& part : Split(relative, L"\\"))
    {
        if (part == L"..")
        {
            error = "the main program path must stay inside the imported folder";
            return false;
        }
    }

    const auto full = std::filesystem::path(imported.source_path) / relative;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(full, ec))
    {
        error = "the main program does not exist: " + WideToUTF8(relative);
        return false;
    }
    if (!EqualsIgnoreCase(full.extension().wstring(), L".exe"))
    {
        error = "the main program must be an executable (.exe) file";
        return false;
    }

    main_program_.preset_id = preset_id;
    main_program_.import_name = import_name;
    main_program_.relative_path = relative;
    has_main_program_ = true;
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
            error = "'" + WideToUTF8(import_name) + "' already exists below "
                    + WideToUTF8(preset.display_name);
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
                                    const std::wstring& file_name, const std::wstring& source_path,
                                    std::string& error)
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
    if (directory.empty())
    {
        error = "the target directory is not a valid relative path";
        return false;
    }

    /*
     * The first segment has to be an imported folder: the file extends an
     * existing lower layer instead of creating a new one.
     */
    const auto segments = Split(directory, L"\\");
    ImportedFolder imported;
    if (!GetImport(preset_id, segments.front(), imported))
    {
        error = "'" + WideToUTF8(segments.front()) + "' is not an imported folder";
        return false;
    }

    /* Store the canonical spelling of the imported folder. */
    std::wstring canonical_dir = imported.import_name;
    const auto remainder = JoinSegments(segments, 1);
    if (!remainder.empty())
    {
        canonical_dir.push_back(L'\\');
        canonical_dir += remainder;
    }

    if (source_path.empty())
    {
        error = "'" + WideToUTF8(file_name) + "' has no source file";
        return false;
    }

    for (const auto& file : imported_files_)
    {
        if (file.preset_id == preset_id && EqualsIgnoreCase(file.target_dir, canonical_dir)
            && EqualsIgnoreCase(file.file_name, file_name))
        {
            error = "'" + WideToUTF8(file_name) + "' was already imported into "
                    + WideToUTF8(canonical_dir);
            return false;
        }
    }

    ImportedFile file;
    file.preset_id = preset_id;
    file.target_dir = canonical_dir;
    file.file_name = file_name;
    file.source_path = source_path;
    imported_files_.push_back(std::move(file));
    return true;
}

bool PackModel::RestoreMainProgram(const std::string& preset_id, const std::wstring& import_name,
                                   const std::wstring& relative_path, std::string& error)
{
    PresetDirectory preset;
    if (!FindPresetDirectory(preset_id, preset))
    {
        error = "unknown preset directory: " + preset_id;
        return false;
    }

    ImportedFolder imported;
    if (!GetImport(preset_id, import_name, imported))
    {
        error = "the imported folder no longer exists";
        return false;
    }

    const auto relative = NormalizeRelativeFile(relative_path);
    if (relative.empty())
    {
        error = "the main program path is empty or leaves the imported folder";
        return false;
    }

    if (!EqualsIgnoreCase(std::filesystem::path(relative).extension().wstring(), L".exe"))
    {
        error = "the main program must be an executable (.exe) file";
        return false;
    }

    main_program_.preset_id = preset_id;
    main_program_.import_name = imported.import_name;
    main_program_.relative_path = relative;
    has_main_program_ = true;
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

bool PackModel::HasMainProgram() const
{
    return has_main_program_;
}

const MainProgram& PackModel::MainProgramChoice() const
{
    return main_program_;
}

bool PackModel::MainProgramPath(std::wstring& path) const
{
    if (!has_main_program_)
    {
        return false;
    }

    ImportedFolder imported;
    if (!GetImport(main_program_.preset_id, main_program_.import_name, imported))
    {
        return false;
    }

    path = (std::filesystem::path(imported.source_path) / main_program_.relative_path).wstring();
    return true;
}

} // namespace appbox
