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
 * @brief One executable the sandboxed application starts.
 *
 * A startup file is an executable of an imported folder. The sandbox starts
 * every startup file whose `auto_start` flag is set; a startup file which is
 * not marked for auto start can be started by naming its `trigger` on the
 * `--X-AppBox-Startup` command line of the packaged loader.
 */
struct StartupFile
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

    /**
     * @brief Trigger name of the startup file.
     *
     * The trigger selects the startup file on the `--X-AppBox-Startup`
     * command line. It is unique ignoring case and defaults to the file name
     * of the executable without its extension.
     */
    std::wstring trigger;

    /**
     * @brief Whether the sandbox starts the file without being asked to.
     */
    bool auto_start = true;
};

/**
 * @brief Get the default trigger of an executable path.
 *
 * The default trigger is the file name without its extension, e.g. `app` for
 * `bin\app.exe`.
 *
 * @param[in] relative_path Executable path relative to an import root.
 * @return The default trigger of the path.
 */
std::wstring DefaultStartupTrigger(const std::wstring& relative_path);

/**
 * @brief Strip the surrounding whitespace of a trigger name.
 *
 * A trigger is typed by the user, so it is stored without the surrounding
 * whitespace: `app ` and `app` would otherwise be two different triggers.
 *
 * @param[in] text Trigger name to trim.
 * @return The trimmed trigger name.
 */
std::wstring TrimStartupTrigger(const std::wstring& text);

/**
 * @brief Get the first trigger of a base name which is still free.
 *
 * The base name itself is used when it is free already; otherwise a numeric
 * suffix is appended (`app`, `app-2`, `app-3`, ...).
 *
 * @param[in] files Startup files which already hold their triggers.
 * @param[in] base Base trigger derived from the file name.
 * @return A trigger which none of the files uses.
 */
std::wstring FreeStartupTrigger(const std::vector<StartupFile>& files, const std::wstring& base);

/**
 * @brief Editable model of a packer session.
 *
 * The model tracks the imported folders per preset directory and the startup
 * files of the packaged application. All validation (name uniqueness,
 * executable checks, trigger uniqueness) happens here so the UI layer stays
 * free of business rules.
 *
 * The startup file list keeps the order the files were added in, which is the
 * order the sandbox starts them in. The first entry also names the loader
 * program inside the archive.
 */
class PackModel
{
public:
    /**
     * @brief Drop every import and every startup file.
     *
     * The model is left in the state of a fresh session, which is the starting
     * point of a project import: the imported content of a project file is
     * restored afterwards.
     */
    void Clear();

    /**
     * @brief Whether the model holds no import and no startup file.
     * @return true when the model describes an empty session.
     */
    bool IsEmpty() const;

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
     * Every startup file pointing at the removed import is dropped as well,
     * and every imported file below the removed folder is dropped.
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
     *
     * A startup file pointing at the removed file is dropped as well, because
     * the file it refers to is no longer part of the archive.
     *
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
     * @brief Get the startup files of the sandboxed application.
     * @return The startup files in startup order.
     */
    const std::vector<StartupFile>& StartupFiles() const;

    /**
     * @brief Whether the model holds at least one startup file.
     * @return true when a startup file is configured.
     */
    bool HasStartupFiles() const;

    /**
     * @brief Whether at least one startup file starts automatically.
     * @return true when a startup file carries the auto start flag.
     */
    bool HasAutoStart() const;

    /**
     * @brief Whether one executable is already a startup file.
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] import_name Name of the imported folder.
     * @param[in] relative_path Executable path relative to the import root.
     * @return true when the executable is in the startup file list.
     */
    bool IsStartupFile(const std::string& preset_id, const std::wstring& import_name,
                       const std::wstring& relative_path) const;

    /**
     * @brief Append one executable to the startup files.
     *
     * The relative path may use forward or backslashes and is normalized to
     * backslashes. The referenced file must exist inside the imported folder
     * and must be an executable. The trigger defaults to the file name without
     * its extension; when that trigger is taken already, a numeric suffix is
     * appended so the list stays unambiguous.
     *
     * An executable which is already a startup file is not added a second
     * time: its auto start flag is updated instead and its trigger is kept.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] import_name Name of the imported folder.
     * @param[in] relative_path Executable path relative to the import root.
     * @param[in] auto_start Whether the sandbox starts the file by itself.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool AddStartupFile(const std::string& preset_id, const std::wstring& import_name,
                        const std::wstring& relative_path, bool auto_start, std::string& error);

    /**
     * @brief Replace the whole startup file list.
     *
     * The operation is atomic: every entry is validated before the list is
     * replaced, so a rejected list leaves the model unchanged. Every entry
     * must name an existing executable of an imported folder and must carry a
     * trigger which is neither empty nor used by another entry (ignoring
     * case).
     *
     * @param[in] files The startup files in startup order.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool SetStartupFiles(std::vector<StartupFile> files, std::string& error);

    /**
     * @brief Restore an imported folder without touching the host filesystem.
     *
     * Unlike ImportFolder(), which derives the import name from the source
     * folder and requires that folder to exist, this entry point stores the
     * given import name and source path as they are. It is used to restore a
     * project file, so the source folder does not have to exist: a project can
     * be imported on a machine where the packaged application is not installed
     * (the pack run reports the missing folder instead).
     *
     * The structural rules of ImportFolder() still apply: the preset directory
     * must be known, the import name must be a plain directory name and it
     * must be unique below the preset (case insensitive, matching the host
     * filesystem).
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] import_name Subdirectory name below the preset directory.
     * @param[in] source_path Host folder which was imported.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool RestoreImportedFolder(const std::string& preset_id, const std::wstring& import_name,
                               const std::wstring& source_path, std::string& error);

    /**
     * @brief Restore an individually imported file without touching the host filesystem.
     *
     * Unlike ImportFiles(), the source file does not have to exist and the
     * target directory does not have to be free of a host file of the same
     * name; the entry is restored as the project file records it. The target
     * directory is normalized like in ImportFiles() and its first segment must
     * be an imported folder of the same preset directory, because an imported
     * file always extends an existing lower layer.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory.
     * @param[in] file_name Name of the file inside the target directory.
     * @param[in] source_path Host file which was imported.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool RestoreImportedFile(const std::string& preset_id, const std::wstring& target_dir,
                             const std::wstring& file_name, const std::wstring& source_path, std::string& error);

    /**
     * @brief Restore one startup file without touching the host filesystem.
     *
     * Unlike AddStartupFile(), the referenced executable does not have to
     * exist; only the shape of the path, the imported folder it lives in and
     * the trigger are validated.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] import_name Name of the imported folder containing the file.
     * @param[in] relative_path Executable path relative to the import root.
     * @param[in] trigger Trigger name of the startup file.
     * @param[in] auto_start Whether the sandbox starts the file by itself.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool RestoreStartupFile(const std::string& preset_id, const std::wstring& import_name,
                            const std::wstring& relative_path, const std::wstring& trigger, bool auto_start,
                            std::string& error);

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
     * @brief Get the full host path of one startup file.
     * @param[in] file The startup file to resolve.
     * @param[out] path The resolved host path when found.
     * @return true when the imported folder of the file still exists.
     */
    bool StartupFilePath(const StartupFile& file, std::wstring& path) const;

    /**
     * @brief Get the host folder behind a folder of the sandbox view.
     *
     * The container of the filesystem view is not a folder of the model and
     * has no host counterpart, a preset directory maps to its resolved host
     * directory, an imported folder maps to the host folder it was imported
     * from, and a folder below an import maps to that host folder extended by
     * the relative path of the folder.
     *
     * The lookup reads the model only, so the host filesystem is never
     * touched: a folder which was imported on a machine where it does not
     * exist still maps to its path.
     *
     * @param[in] preset_id Identifier of the preset directory, empty for the
     *            container.
     * @param[in] import_name Name of the imported folder, empty for a preset
     *            directory.
     * @param[in] relative_dir Folder path relative to the import root, empty
     *            for import roots.
     * @param[out] path The host folder when it is known, untouched otherwise.
     * @return true when the folder maps to a host folder.
     */
    bool HostFolderPath(const std::string& preset_id, const std::wstring& import_name, const std::wstring& relative_dir,
                        std::wstring& path) const;

private:
    /**
     * @brief Resolve and validate one startup file reference.
     *
     * The preset directory and the imported folder have to exist, the path has
     * to stay inside the imported folder and has to name an executable. When
     * `require_host` is set the file also has to exist on the host.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] import_name Name of the imported folder.
     * @param[in] relative_path Executable path relative to the import root.
     * @param[in] require_host Whether the executable must exist on the host.
     * @param[out] out The resolved startup file without its trigger.
     * @param[out] error Error description on failure.
     * @return true when the reference is usable.
     */
    bool ResolveStartupFile(const std::string& preset_id, const std::wstring& import_name,
                            const std::wstring& relative_path, bool require_host, StartupFile& out,
                            std::string& error) const;

    std::vector<ImportedFolder> imports_;
    std::vector<ImportedFile>   imported_files_;
    std::vector<StartupFile>    startup_files_;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_PACK_MODEL_HPP
