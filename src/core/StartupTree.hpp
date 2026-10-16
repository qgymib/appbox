#ifndef APPBOX_PACKER_CORE_STARTUP_TREE_HPP
#define APPBOX_PACKER_CORE_STARTUP_TREE_HPP

#include "PackModel.hpp"
#include <memory>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Kind of one row of the startup file tree.
 */
enum class StartupNodeKind
{
    Container,        ///< The container holding the preset directories.
    Preset,           ///< A preset directory of the packer.
    Import,           ///< An imported folder below a preset directory.
    Directory,        ///< A host subfolder of an imported folder.
    VirtualDirectory, ///< A folder the user created inside the sandbox view.
    Executable        ///< A file which can become a startup file.
};

/**
 * @brief One row of the startup file tree.
 *
 * The container, the preset, the import and the folder rows are built from the
 * pack model and from the host folders of the imported folders, the executable
 * rows are the startable files below them. Every node owns its children, so the
 * tree is destroyed as a whole.
 *
 * A row which names a folder of the sandbox view carries the path of the folder
 * relative to its preset directory in `virtual_dir`; a row which names a file
 * carries the path of that file inside the layer in `import_name` and
 * `relative_path`, which is the way a startup file of the model is stored.
 */
struct StartupNode
{
    /**
     * @brief Kind of the row.
     */
    StartupNodeKind kind = StartupNodeKind::Directory;

    /**
     * @brief Name shown in the name column.
     */
    std::wstring label;

    /**
     * @brief Host path of the folder or file behind the row.
     *
     * The container and a folder the user created map to no host location, so
     * their path stays empty.
     */
    std::wstring host_path;

    /**
     * @brief Identifier of the owning preset directory, empty for the container.
     */
    std::string preset_id;

    /**
     * @brief First segment of the path of the row inside the layer.
     *
     * The segment is the name of the imported folder or of the folder of the
     * view which holds the row; it is empty for a row which names the layer
     * root of its preset directory or a file which lives in that root.
     */
    std::wstring import_name;

    /**
     * @brief Path relative to the import root using backslashes, empty for the
     *        folder rows which name the import root itself.
     */
    std::wstring relative_path;

    /**
     * @brief Path of a folder the user created, relative to its preset
     *        directory, empty for every other row.
     */
    std::wstring virtual_dir;

    /**
     * @brief Owning node, null for the container.
     */
    StartupNode* parent = nullptr;

    /**
     * @brief Children of the row.
     */
    std::vector<std::unique_ptr<StartupNode>> children;
};

/**
 * @brief Tree of the files a packaged application can start.
 *
 * The tree mirrors the filesystem workspace of the packer: the container with
 * the preset directories, the nested presets below them, the imported folders
 * and, below an import, the host subfolders, the folders the user created and
 * the files which were imported on their own. Only the startable files of the
 * tree are listed, see `common/StartupFileType.hpp`.
 *
 * A row is only offered while it holds a startable file below it, which is what
 * makes the tree show the folders on the way to a file instead of the whole
 * sandbox view. A startup file of the model is kept either way, so a project
 * which is imported on a machine where the packaged application is not
 * installed still lists its startup files and the user can drop them.
 *
 * The tree owns the startup file list the dialog edits: every executable row
 * reports whether it is a startup file, whether it starts automatically and
 * which trigger it carries. The list keeps the order the files were added in,
 * which is the order the sandbox starts them in.
 *
 * The class holds no wxWidgets dependency, so the tree and the rules of the
 * startup file list are unit testable.
 */
class StartupTree
{
public:
    /**
     * @brief Build the tree of the model.
     *
     * The constructor reads the host folders of the imported folders, so it is
     * meant to run on a worker thread while the dialog shows that it is
     * scanning. A folder which cannot be read stays empty and is not offered.
     *
     * @param[in] model The pack model holding the imports and the startup files.
     */
    explicit StartupTree(const PackModel& model);

    /**
     * @brief Get the rows of the tree.
     * @return The container row, which is the only row of the tree.
     */
    const std::vector<std::unique_ptr<StartupNode>>& Roots() const;

    /**
     * @brief Get the rows which hold children, in the order of the tree.
     *
     * The startup files dialog expands every row it gets, so it opens with the
     * whole tree in view instead of the rows on the way to a startup file only.
     * A row which names a startable file never holds children, which is why it
     * is never part of the list.
     *
     * @return The rows with at least one child, the container row first.
     */
    std::vector<const StartupNode*> ExpandableRows() const;

    /**
     * @brief Find the row of a path below an import root.
     *
     * @param[in] import_root The import row to search below.
     * @param[in] relative_path Path relative to the import root.
     * @return The matching row, null when it does not exist.
     */
    StartupNode* FindNode(StartupNode& import_root, const std::wstring& relative_path) const;

    /**
     * @brief Find the row of a startup file.
     *
     * The preset is looked up through the whole tree, so a nested preset is
     * found as well; the import and the folders of the way to the file are
     * matched by name ignoring the case.
     *
     * @param[in] file The startup file to look up.
     * @return The matching row, null when it does not exist.
     */
    StartupNode* FindChoice(const StartupFile& file) const;

    /**
     * @brief Whether a row can be a startup file.
     * @param[in] node Row to test.
     * @return true for the rows which name a startable file.
     */
    static bool IsCheckable(const StartupNode& node);

    /**
     * @brief Get the startup file list of the tree.
     * @return The startup files in startup order.
     */
    const std::vector<StartupFile>& Files() const;

    /**
     * @brief Whether the tree holds at least one startup file.
     * @return true when a startup file is in the list.
     */
    bool HasFiles() const;

    /**
     * @brief Whether a row is a startup file.
     * @param[in] node Row to test.
     * @return true when the row is in the startup file list.
     */
    bool Contains(const StartupNode& node) const;

    /**
     * @brief Whether a row starts automatically.
     * @param[in] node Row to test.
     * @return true when the row is a startup file with the auto start flag.
     */
    bool IsAutoStart(const StartupNode& node) const;

    /**
     * @brief Get the trigger of a row.
     * @param[in] node Row to test.
     * @return The trigger of the row, empty when it is not a startup file.
     */
    std::wstring TriggerOf(const StartupNode& node) const;

    /**
     * @brief Set the auto start flag of a row.
     *
     * A row which is not a startup file yet is appended to the list with the
     * default trigger. Clearing the flag of a row which is not in the list
     * does nothing.
     *
     * @param[in] node Row to change.
     * @param[in] auto_start New state of the flag.
     * @return true when the row is a startable file and the list was changed.
     */
    bool SetAutoStart(const StartupNode& node, bool auto_start);

    /**
     * @brief Set the trigger of a row.
     *
     * A row which is not a startup file yet is appended to the list. The
     * trigger must not be empty and must not be used by another startup file
     * of the tree, ignoring case.
     *
     * @param[in] node Row to change.
     * @param[in] trigger New trigger of the row.
     * @param[out] error Error description on failure.
     * @return true when the row is a startable file and the trigger was stored.
     */
    bool SetTrigger(const StartupNode& node, const std::wstring& trigger, std::string& error);

    /**
     * @brief Drop a row from the startup file list.
     * @param[in] node Row to drop.
     * @return true when the row was a startup file and was dropped.
     */
    bool Remove(const StartupNode& node);

private:
    /**
     * @brief Append the row of one preset directory and everything below it.
     * @param[in,out] parent Row the preset hangs below.
     * @param[in] preset Preset directory to append.
     */
    void AppendPreset(StartupNode& parent, const PresetDirectory& preset);

    /**
     * @brief Append the row of one imported folder and everything below it.
     * @param[in,out] preset_node Preset directory owning the import.
     * @param[in] imported Imported folder to append.
     */
    void AppendImport(StartupNode& preset_node, const ImportedFolder& imported);

    /**
     * @brief Append the content of one folder of the sandbox view.
     *
     * The host subfolders and the startable files of the host folder are
     * listed, followed by the files which were imported on their own and the
     * folders the user created inside the folder. A folder the user created has
     * no host counterpart, so its host content is skipped.
     *
     * @param[in,out] node Folder row to fill.
     */
    void AppendFolder(StartupNode& node);

    /**
     * @brief Append the row of one folder the user created and everything below it.
     * @param[in,out] parent Row the folder hangs below.
     * @param[in] target_dir Path of the folder relative to the preset directory.
     */
    void AppendVirtualFolder(StartupNode& parent, const std::wstring& target_dir);

    /**
     * @brief Append the startable files which were imported into one folder.
     * @param[in,out] node Folder row to fill.
     */
    void AppendImportedFiles(StartupNode& node);

    /**
     * @brief Find or create the row of one preset directory.
     *
     * A nested preset is created below the preset which holds it, so the shape
     * of the tree follows the table of the preset directories.
     *
     * @param[in] preset_id Identifier of the preset directory.
     * @return The row of the preset, null when the identifier is unknown.
     */
    StartupNode* EnsurePreset(const std::string& preset_id);

    /**
     * @brief Find or create the row of one preset directory below a row.
     * @param[in,out] parent Row to search in.
     * @param[in] preset_id Identifier of the preset directory.
     * @return The row of the preset, null when the identifier is unknown.
     */
    StartupNode* EnsurePresetBelow(StartupNode& parent, const std::string& preset_id);

    /**
     * @brief Find the row of one preset directory below a row.
     *
     * The lookup walks the whole subtree, so a nested preset is found as well.
     *
     * @param[in] parent Row to search in.
     * @param[in] preset_id Identifier of the preset directory.
     * @return The row of the preset, null when the subtree holds none.
     */
    static StartupNode* FindPresetBelow(StartupNode& parent, const std::string& preset_id);

    /**
     * @brief Find or create the row of one startup file of the model.
     *
     * The file is placed below the folders of the path it carries inside the
     * layer of its preset directory, and the folders of the way to the file are
     * created as folders of the view when the tree does not hold them yet: that
     * is what keeps a startup file whose host file is gone in the tree, so the
     * user can see it and drop it.
     *
     * @param[in] file The startup file to place.
     * @return The row of the file, null when its path names no file.
     */
    StartupNode* EnsureChoice(const StartupFile& file);

    /**
     * @brief Drop every row which holds no startable file below it.
     * @param[in,out] node Row whose children are pruned.
     */
    static void PruneEmpty(StartupNode& node);

    /**
     * @brief Append the rows of one subtree which hold children.
     * @param[in] node Row to inspect.
     * @param[out] rows Rows which hold children, in the order of the tree.
     */
    static void CollectExpandable(const StartupNode& node, std::vector<const StartupNode*>& rows);

    /**
     * @brief Order the children of one row and of everything below it.
     * @param[in,out] node Row whose children are ordered.
     */
    static void SortChildren(StartupNode& node);

    /**
     * @brief Get the folder of the sandbox view one row names.
     * @param[in] node Row to inspect.
     * @return The path of the folder relative to the preset directory.
     */
    static std::wstring TargetDirOf(const StartupNode& node);

    /**
     * @brief Build the path of a file relative to the first segment of its path.
     * @param[in] target_dir Directory of the file relative to the preset directory.
     * @param[in] file_name Name of the file inside the directory.
     * @return The path of the file relative to the root of its layer, which is
     *         the name of the file alone when the directory is the layer root.
     */
    static std::wstring ImportRelativePath(const std::wstring& target_dir, const std::wstring& file_name);

    /**
     * @brief Whether one row and one startup file describe the same file.
     * @param[in] node Row to test.
     * @param[in] file Startup file to test.
     * @return true when both name the same file of the same import.
     */
    static bool Matches(const StartupNode& node, const StartupFile& file);

    /**
     * @brief Build the startup file of a row.
     * @param[in] node Executable row.
     * @return The startup file of the row without its trigger.
     */
    static StartupFile MakeStartupFile(const StartupNode& node);

    /**
     * @brief Get the index of a row inside the startup file list.
     * @param[in] node Row to look up.
     * @return The index, -1 when the row is not a startup file.
     */
    int IndexOf(const StartupNode& node) const;

    /**
     * @brief The container row, null while the tree was not built.
     */
    StartupNode* container_ = nullptr;

    /**
     * @brief The rows of the tree, which hold the container row.
     */
    std::vector<std::unique_ptr<StartupNode>> roots_;

    /**
     * @brief The startup file list of the tree, in startup order.
     */
    std::vector<StartupFile> files_;

    /**
     * @brief The model the tree is built from.
     *
     * The tree reads the model while it is built and holds no reference to it
     * afterwards, so a caller may build a tree from a model it owns and let the
     * model go once the constructor returned.
     */
    const PackModel* model_ = nullptr;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_STARTUP_TREE_HPP
