#ifndef APPBOX_PACKER_CORE_PACK_MODEL_HPP
#define APPBOX_PACKER_CORE_PACK_MODEL_HPP

#include "PresetDirectory.hpp"
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief A host folder imported below one preset directory.
 *
 * At sandbox runtime the folder content is visible as
 * `<preset>\<import_name>` inside the sandbox view.
 */
struct ImportedFolder
{
    /**
     * @brief Identifier of the owning preset directory.
     */
    std::string preset_id;

    /**
     * @brief Subdirectory name below the preset directory.
     */
    std::wstring import_name;

    /**
     * @brief Host folder which was imported.
     */
    std::wstring source_path;
};

/**
 * @brief A single host file imported below one preset directory.
 *
 * At sandbox runtime the file is visible as
 * `<preset>\<target_dir>\<file_name>` inside the sandbox view. The first
 * segment of the target directory is always the name of an imported folder,
 * so an imported file extends an existing lower layer instead of creating a
 * new one.
 */
struct ImportedFile
{
    /**
     * @brief Identifier of the owning preset directory.
     */
    std::string preset_id;

    /**
     * @brief Directory relative to the preset directory, e.g. `L"MyApp\data"`.
     */
    std::wstring target_dir;

    /**
     * @brief Name of the file inside the target directory.
     */
    std::wstring file_name;

    /**
     * @brief Host file which was imported.
     */
    std::wstring source_path;
};

/**
 * @brief The main executable of the sandboxed application.
 */
struct MainProgram
{
    /**
     * @brief Identifier of the preset directory owning the imported folder.
     */
    std::string preset_id;

    /**
     * @brief Imported folder containing the executable.
     */
    std::wstring import_name;

    /**
     * @brief Executable path relative to the imported folder root.
     */
    std::wstring relative_path;
};

/**
 * @brief Editable model of a packer session.
 *
 * The model tracks the imported folders per preset directory and the main
 * program choice. All validation (name uniqueness, executable checks)
 * happens here so the UI layer stays free of business rules.
 */
class PackModel
{
public:
    /**
     * @brief Import a host folder as a subdirectory of a preset directory.
     *
     * The import name is the last path component of the source folder. The
     * operation fails when the preset is unknown, the source is not a
     * directory, or a folder with the same name was already imported below
     * the preset (case insensitive, matching the host filesystem).
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] source_path Host folder to import.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool ImportFolder(const std::string& preset_id, const std::wstring& source_path, std::string& error);

    /**
     * @brief Remove a previously imported folder.
     *
     * A main program pointing at the removed import is cleared as well, and
     * every imported file below the removed folder is dropped.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] import_name Name of the imported folder.
     */
    void RemoveImport(const std::string& preset_id, const std::wstring& import_name);

    /**
     * @brief Import individual host files into a folder of the sandbox view.
     *
     * The target directory is relative to the preset directory and its first
     * segment must be an already imported folder. Forward and backslashes are
     * accepted; the stored directory is normalized to backslashes. The
     * operation is atomic: either every selected file is imported or the model
     * stays unchanged.
     *
     * The call fails when the preset is unknown, the target directory is not a
     * valid relative path below an imported folder, a source file is missing,
     * a file with the same name already exists in the imported folder, or the
     * name collides with an earlier import.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory.
     * @param[in] source_paths Host files to import.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool ImportFiles(const std::string& preset_id, const std::wstring& target_dir,
                     const std::vector<std::wstring>& source_paths, std::string& error);

    /**
     * @brief Remove a previously imported file.
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory.
     * @param[in] file_name Name of the file inside the target directory.
     * @return true when an import was removed.
     */
    bool RemoveImportedFile(const std::string& preset_id, const std::wstring& target_dir,
                            const std::wstring& file_name);

    /**
     * @brief Get the imported files of one directory.
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory.
     * @return The imported files in import order.
     */
    std::vector<ImportedFile> FilesOf(const std::string& preset_id, const std::wstring& target_dir) const;

    /**
     * @brief Get every imported file of the model.
     * @return The imported files in import order.
     */
    const std::vector<ImportedFile>& AllImportedFiles() const;

    /**
     * @brief Select the main program of the sandboxed application.
     *
     * The relative path may use forward or backslashes and is normalized to
     * backslashes. The referenced file must exist inside the imported folder
     * and must be an executable.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] import_name Name of the imported folder.
     * @param[in] relative_path Executable path relative to the import root.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool SetMainProgram(const std::string& preset_id, const std::wstring& import_name,
                        const std::wstring& relative_path, std::string& error);

    /**
     * @brief Get the imports of one preset directory.
     * @param[in] preset_id Identifier of the preset directory.
     * @return The imported folders in import order.
     */
    std::vector<ImportedFolder> ImportsOf(const std::string& preset_id) const;

    /**
     * @brief Get one import by preset and name.
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] import_name Name of the imported folder.
     * @param[out] out The imported folder description when found.
     * @return true when the import exists.
     */
    bool GetImport(const std::string& preset_id, const std::wstring& import_name, ImportedFolder& out) const;

    /**
     * @brief Whether a main program is selected.
     * @return true when a main program is set.
     */
    bool HasMainProgram() const;

    /**
     * @brief Get the selected main program.
     * @return The main program description; only valid when
     *         HasMainProgram() returns true.
     */
    const MainProgram& MainProgramChoice() const;

    /**
     * @brief Get the full host path of the selected main program.
     * @param[out] path The resolved host path when found.
     * @return true when a main program is set.
     */
    bool MainProgramPath(std::wstring& path) const;

private:
    std::vector<ImportedFolder> imports_;
    std::vector<ImportedFile>   imported_files_;
    MainProgram                 main_program_;
    bool                        has_main_program_ = false;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_PACK_MODEL_HPP
