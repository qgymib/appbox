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
 * @brief A folder of the sandbox view which has no host counterpart.
 *
 * The record describes a folder the user created inside the packer: it is an
 * entry of the layer of its preset directory, so at sandbox runtime it is
 * visible as `<preset>\<target_dir>` and holds whatever is imported into it.
 * Unlike an imported folder it maps to no folder of the host, so the pack run
 * writes the directory itself into the archive instead of copying a folder.
 */
struct VirtualDirectory
{
    /**
     * @brief Identifier of the owning preset directory.
     */
    std::string preset_id;

    /**
     * @brief Directory relative to the preset directory, e.g. `L"MyApp\logs"`.
     *
     * The path is never empty: a folder which was created always names a
     * subdirectory of the layer of its preset directory.
     */
    std::wstring target_dir;
};

/**
 * @brief An entry of the sandbox view the archive hides.
 *
 * The record describes an entry of the host filesystem the packaged
 * application must not see. The pack run writes the whiteout marker of the
 * entry (`<name>.$APPBOX_DELETE$`) into the layer of its preset directory, and
 * a marker which any layer of the view holds is honoured at run time, so the
 * archive hides the entry of the host as well.
 *
 * Unlike an imported folder the record does not name a host folder which has
 * to exist: the marker is a name of the view, so the layer only carries the
 * directory chain which holds it. The record is what the capture workspace
 * writes for an entry a snapshot reports as removed.
 */
struct WhiteoutEntry
{
    /**
     * @brief Identifier of the owning preset directory.
     */
    std::string preset_id;

    /**
     * @brief Directory relative to the preset directory, empty for the layer
     *        root.
     */
    std::wstring target_dir;

    /**
     * @brief Name of the entry inside the target directory.
     */
    std::wstring name;
};

/**
 * @brief One file the sandboxed application starts.
 *
 * A startup file is a startable file of an imported folder: a program
 * (`.exe`), a batch file (`.bat`), a command script (`.cmd`) or a PowerShell
 * script (`.ps1`), see `common/StartupFileType.hpp`. The sandbox starts every
 * startup file whose `auto_start` flag is set; a startup file which is not
 * marked for auto start can be started by naming its `trigger` on the
 * `--X-AppBox-Startup` command line of the packaged launcher.
 */
struct StartupFile
{
    /**
     * @brief Identifier of the preset directory owning the imported folder.
     */
    std::string preset_id;

    /**
     * @brief Imported folder containing the file.
     */
    std::wstring import_name;

    /**
     * @brief Path of the file relative to the imported folder root.
     */
    std::wstring relative_path;

    /**
     * @brief Trigger name of the startup file.
     *
     * The trigger selects the startup file on the `--X-AppBox-Startup`
     * command line. It is unique ignoring case and defaults to the file name
     * of the file without its extension.
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
 * The model tracks the imported folders per preset directory, the folders the
 * user created inside the sandbox view and the startup files of the packaged
 * application. All validation (name uniqueness, startable file checks, trigger
 * uniqueness) happens here so the UI layer stays free of business rules.
 *
 * A folder the user created has no host counterpart: it is an entry of the
 * layer of its preset directory, which the pack run writes into the archive as
 * a directory of its own. The folders are stored as the path relative to the
 * preset directory, so a folder and everything below it is one prefix of a
 * path and renaming or removing a folder is a prefix operation.
 *
 * The startup file list keeps the order the files were added in, which is the
 * order the sandbox starts them in. The first entry also names the launcher
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
     * @brief Whether the model holds no import, no created folder and no startup file.
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
     * @brief Get the default name of a new folder inside a directory.
     *
     * The name `New Folder` is used while it is free inside the directory;
     * otherwise a numeric suffix is appended (`New Folder 2`, `New Folder 3`,
     * ...), the way FreeStartupTrigger() names the triggers of the startup
     * files. A name is free when the directory of the view holds no entry of
     * it, see CreateVirtualDirectory().
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] parent_dir Directory relative to the preset directory, empty
     *            for the layer root.
     * @return The default name of a new folder.
     */
    std::wstring FreeVirtualDirectoryName(const std::string& preset_id, const std::wstring& parent_dir) const;

    /**
     * @brief Create an empty folder inside a directory of the sandbox view.
     *
     * The folder is an entry of the layer of the preset directory and has no
     * host counterpart: the host filesystem is only read to decide whether a
     * name is still free, never written.
     *
     * The operation fails when the preset is unknown, the parent directory is
     * not a folder of the view, the name is not a plain folder name, or the
     * name is taken inside the parent by a folder the user created, an
     * imported folder, an imported file or an entry of the host folder the
     * parent maps to.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] parent_dir Directory relative to the preset directory, empty
     *            for the layer root.
     * @param[in] name Name of the new folder.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool CreateVirtualDirectory(const std::string& preset_id, const std::wstring& parent_dir, const std::wstring& name,
                                std::string& error);

    /**
     * @brief Rename a folder the user created, together with everything below it.
     *
     * The folder and every folder below it are stored under the new path, so
     * the subtree keeps its shape. The operation fails when the folder is not
     * one the user created, the new name is not a plain folder name, or the
     * new name is taken next to the folder, see CreateVirtualDirectory().
     *
     * A rename to the name the folder carries today is accepted and changes
     * nothing.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory.
     * @param[in] new_name New name of the folder.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool RenameVirtualDirectory(const std::string& preset_id, const std::wstring& target_dir,
                                const std::wstring& new_name, std::string& error);

    /**
     * @brief Remove a folder the user created and everything below it.
     *
     * The imported files below the folder have no directory of the view
     * anymore and are dropped together with the startup files which point at
     * them, exactly like RemoveImport() drops the content of an import.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory.
     * @return true when at least one folder was removed.
     */
    bool RemoveVirtualDirectory(const std::string& preset_id, const std::wstring& target_dir);

    /**
     * @brief Get the folders the user created inside one directory.
     *
     * Only the direct children are reported: a folder below a child is reached
     * by asking for that child.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory, empty
     *            for the layer root.
     * @return The folders in the order they were created in.
     */
    std::vector<VirtualDirectory> VirtualDirectoriesOf(const std::string&  preset_id,
                                                       const std::wstring& target_dir) const;

    /**
     * @brief Get every folder the user created.
     * @return The folders in the order they were created in.
     */
    const std::vector<VirtualDirectory>& AllVirtualDirectories() const;

    /**
     * @brief Whether the user created the folder of a path.
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory.
     * @return true when the path names a folder the user created.
     */
    bool HasVirtualDirectory(const std::string& preset_id, const std::wstring& target_dir) const;

    /**
     * @brief Hide one entry of the host filesystem from the packaged application.
     *
     * The target directory is relative to the preset directory and may be
     * empty, which names the layer root of the preset directory. The parent of
     * the entry does not have to be a folder of the view: a snapshot which
     * reports the delete of an entry of a folder nothing was imported from
     * still describes a delete of the view.
     *
     * The operation fails when the preset is unknown, the target directory is
     * not a valid relative path, the name is not a plain name, the name is a
     * name the view reserves for its markers, or the entry is hidden already.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory.
     * @param[in] name Name of the entry to hide.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool AddWhiteout(const std::string& preset_id, const std::wstring& target_dir, const std::wstring& name,
                     std::string& error);

    /**
     * @brief Stop hiding an entry of the host filesystem.
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory.
     * @param[in] name Name of the entry.
     * @return true when the entry was hidden and is not anymore.
     */
    bool RemoveWhiteout(const std::string& preset_id, const std::wstring& target_dir, const std::wstring& name);

    /**
     * @brief Stop hiding every entry of a directory and of everything below it.
     *
     * The call is the cascade of a removed folder: the entries the folder hid
     * have no parent of the view anymore.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory, empty
     *            for the layer root.
     * @return true when at least one entry was dropped.
     */
    bool RemoveWhiteoutsUnder(const std::string& preset_id, const std::wstring& target_dir);

    /**
     * @brief Get the entries a directory of the view hides.
     *
     * Only the direct children are reported: an entry below a child is reached
     * by asking for that child.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory, empty
     *            for the layer root.
     * @return The entries in the order they were added in.
     */
    std::vector<WhiteoutEntry> WhiteoutsOf(const std::string& preset_id, const std::wstring& target_dir) const;

    /**
     * @brief Get every entry the archive hides.
     * @return The entries in the order they were added in.
     */
    const std::vector<WhiteoutEntry>& AllWhiteouts() const;

    /**
     * @brief Whether a path of the view is hidden.
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] path Path relative to the preset directory.
     * @return true when the path is hidden.
     */
    bool HasWhiteout(const std::string& preset_id, const std::wstring& path) const;

    /**
     * @brief Restore an entry the archive hides without touching the host filesystem.
     *
     * Unlike AddWhiteout(), which is called for an entry the user picked, this
     * entry point restores a record of a stored document, so it only validates
     * the shape of the record and refuses a path which is hidden already.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory.
     * @param[in] name Name of the entry.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool RestoreWhiteout(const std::string& preset_id, const std::wstring& target_dir, const std::wstring& name,
                         std::string& error);

    /**
     * @brief Whether a directory of a preset directory is a folder of the view.
     *
     * A folder of the view is the layer root of a preset directory, an imported
     * folder, a folder below an import and a folder the user created together
     * with everything below it. The host filesystem is only read for a folder
     * below an import, exactly like DirectoryParentExists() does it.
     *
     * The lookup is what tells a hidden entry which hangs below a folder of the
     * view apart from one which does not: the first is shown by the folder
     * which holds it, the second has no parent the workspace shows.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory, empty
     *            for the layer root.
     * @return true when the path names a folder of the view.
     */
    bool IsDirectoryOfView(const std::string& preset_id, const std::wstring& target_dir) const;

    /**
     * @brief Restore a folder the user created without touching the host filesystem.
     *
     * Unlike CreateVirtualDirectory(), which derives the parent of the new folder from
     * the directory the user is in and checks it against the host, this entry
     * point stores the path as the document records it: a project can be
     * imported on a machine where the packaged application is not installed,
     * so neither the folder of the import nor the host content below it has to
     * exist. The structural rules still apply: the preset directory has to be
     * known, the path has to stay inside the layer and its parent has to be a
     * folder of the view.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool RestoreVirtualDirectory(const std::string& preset_id, const std::wstring& target_dir, std::string& error);

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
     * @brief Append one startable file to the startup files.
     *
     * The relative path may use forward or backslashes and is normalized to
     * backslashes. The referenced file must exist inside the imported folder
     * and must be a startable file (`.exe`, `.bat`, `.cmd` or `.ps1`). The
     * trigger defaults to the file name without its extension; when that
     * trigger is taken already, a numeric suffix is appended so the list stays
     * unambiguous.
     *
     * A file which is already a startup file is not added a second time: its
     * auto start flag is updated instead and its trigger is kept.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] import_name Name of the imported folder.
     * @param[in] relative_path Path of the file relative to the import root.
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
     * must name an existing startable file of an imported folder and must carry
     * a trigger which is neither empty nor used by another entry (ignoring
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
     * Unlike AddStartupFile(), the referenced file does not have to exist; only
     * the shape of the path, the imported folder it lives in and the trigger
     * are validated.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] import_name Name of the imported folder containing the file.
     * @param[in] relative_path Path of the file relative to the import root.
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
     * to stay inside the imported folder and has to name a startable file. When
     * `require_host` is set the file also has to exist on the host.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] import_name Name of the imported folder.
     * @param[in] relative_path Path of the file relative to the import root.
     * @param[in] require_host Whether the executable must exist on the host.
     * @param[out] out The resolved startup file without its trigger.
     * @param[out] error Error description on failure.
     * @return true when the reference is usable.
     */
    bool ResolveStartupFile(const std::string& preset_id, const std::wstring& import_name,
                            const std::wstring& relative_path, bool require_host, StartupFile& out,
                            std::string& error) const;

    /**
     * @brief Whether a directory of the view holds a folder of a path.
     *
     * A parent is a folder of the view when it is the layer root of its preset
     * directory, a folder the user created, an imported folder or a folder
     * below an imported folder. The last case is the only one which is not
     * decided by the model alone: a folder below an import lives in the host
     * folder of the import, so it is asked for there while \p require_host is
     * set.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] parent_dir Directory relative to the preset directory, empty
     *            for the layer root.
     * @param[in] require_host Whether a folder below an import has to exist in
     *            the host folder of the import.
     * @return true when the path names a folder of the view.
     */
    bool DirectoryParentExists(const std::string& preset_id, const std::wstring& parent_dir, bool require_host) const;

    /**
     * @brief Get the host folder a directory of the view maps to.
     *
     * The layer root of a preset directory maps to the real folder of that
     * preset, an imported folder and a folder below it map to the host folder
     * of the import, and a folder the user created maps to none, because it is
     * an entry of the layer alone.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] target_dir Directory relative to the preset directory, empty
     *            for the layer root.
     * @param[out] path The host folder when the directory maps to one.
     * @return true when the directory maps to a host folder.
     */
    bool HostFolderOf(const std::string& preset_id, const std::wstring& target_dir, std::wstring& path) const;

    /**
     * @brief Check whether a name can be used for a new folder inside a directory.
     *
     * The name is usable when the directory of the view holds no entry of it:
     * no folder the user created, no imported folder, no imported file and no
     * entry of the host folder the directory maps to. The host filesystem is
     * only read, so a name stays usable while the imported folder is missing
     * on this machine.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] parent_dir Directory relative to the preset directory, empty
     *            for the layer root.
     * @param[in] name Name to check.
     * @return An empty text when the name is free, otherwise the reason.
     */
    std::string CheckVirtualDirectoryName(const std::string& preset_id, const std::wstring& parent_dir,
                                          const std::wstring& name) const;

    /**
     * @brief Whether an imported file lives at a path of the view.
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] path Path relative to the preset directory.
     * @return true when an imported file of the path exists.
     */
    bool HasImportedFileAt(const std::string& preset_id, const std::wstring& path) const;

    /**
     * @brief Store the canonical spelling of the imported folder a path starts with.
     *
     * The first component of a path below a preset directory is the name of an
     * imported folder whenever the path names a folder inside an import, so it
     * is replaced by the spelling the model stores, exactly like the target
     * directory of an imported file is normalized. A path which starts with a
     * folder the user created is returned unchanged.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] path Normalized path relative to the preset directory.
     * @return The path with the canonical spelling of its import.
     */
    std::wstring CanonicalDirectoryPath(const std::string& preset_id, const std::wstring& path) const;

    std::vector<ImportedFolder>   imports_;
    std::vector<ImportedFile>     imported_files_;
    std::vector<VirtualDirectory> directories_;
    std::vector<WhiteoutEntry>    whiteouts_;
    std::vector<StartupFile>      startup_files_;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_PACK_MODEL_HPP
