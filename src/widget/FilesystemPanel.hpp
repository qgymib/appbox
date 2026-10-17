#ifndef APPBOX_PACKER_WIDGET_FILESYSTEM_PANEL_HPP
#define APPBOX_PACKER_WIDGET_FILESYSTEM_PANEL_HPP

#include <wx/wx.h>
#include <wx/dataview.h>
#include <wx/treectrl.h>
#include "core/FilesystemIsolationModel.hpp"
#include "core/PackModel.hpp"
#include <string>
#include <vector>

class wxSearchCtrl;

/**
 * @brief Filesystem workspace of the packer.
 *
 * Left side: the tree of the virtual filesystem. Its top item is the
 * `Sandbox Filesystem` container with the preset directories below it. A
 * preset directory can hold nested preset directories of its own (the folders
 * of the user profile hang below `Current User Directory`), and every preset
 * directory holds its imported folders, the folders the user created and the
 * host subfolders of the imports, expanded on demand. Right side: a toolbar
 * row (Add Files, Add Folder, New Folder, Remove, Up Dir and a search box)
 * above the report style list of the selected folder.
 *
 * The list shows the filename, the isolation mode, the size and the virtual
 * source path of every entry, following the layout of the reference packaging
 * tool. The mode of a row is picked from a dropdown in the row itself: a
 * folder offers `Full`, `Write Copy`, `Merge` and `Whiteout`, a file offers
 * `Full` and `Whiteout`. A row which the user never touched shows the mode it
 * inherits
 * from the closest folder above it, so the mode of a folder reaches the
 * entries below it. The container lists the top level preset directories;
 * every preset directory is fixed, so it can neither be removed nor renamed.
 * A nested preset directory is offered by the same commands as a top level
 * one: it accepts imported folders and imported files, and it can be entered
 * from the list.
 *
 * The `New Folder` command creates an empty folder inside the folder the tree
 * selects, which is the entry the list below it shows. The folder has no host
 * counterpart: it is an entry of the layer of its preset directory, which the
 * pack run writes into the archive, so the packaged application sees it and can
 * write into it. The command is offered for every node but the container,
 * because the container holds the preset directories and is not a folder of
 * the view. A folder which was created carries a default name and is renamed
 * in the list: the `Filename` column of such a row is edited in place, exactly
 * like a name of the Windows Explorer is, while every other row keeps its name.
 *
 * The isolation mode of a folder is picked from the context menu of the tree,
 * which reaches every node of the view: the dialog it opens offers the modes a
 * folder accepts and the option to apply the chosen mode to the subfolders as
 * well. The container is the root of the view, so the mode picked for it
 * decides every path no other entry covers, including the locations outside
 * the virtual filesystem.
 *
 * The same context menu carries the `Delete` command, which is offered for
 * every node but reaches only the two kinds the view may remove: the imported
 * folders and the folders the user created. The container, the fixed preset
 * directories and the host subfolders below an import - which belong to the
 * import holding them - grey the item out, so the command is visible without
 * being offered where the model would refuse it.
 *
 * The `Up Dir` command moves the tree selection to the node above the selected
 * one. The container is the root of the tree and the only node without a
 * parent, so the command is offered for every node but the container: a top
 * level preset directory moves to the container like a nested one moves to the
 * preset it hangs below.
 *
 */
class FilesystemPanel : public wxPanel
{
public:
    /**
     * @brief Create the filesystem workspace.
     * @param[in] parent Parent window.
     * @param[in,out] model The shared pack model.
     * @param[in,out] isolation The shared isolation modes of the view.
     */
    FilesystemPanel(wxWindow* parent, appbox::PackModel& model, appbox::FilesystemIsolationModel& isolation);

    /**
     * @brief Rebuild tree and list after the model changed externally.
     *
     * The folder the user selected is looked up again after the rebuild, so
     * the workspace stays where it was; a folder which the model no longer
     * holds falls back to the container.
     */
    void RefreshModel();

private:
    /**
     * @brief Location of a tree item inside the sandbox view.
     *
     * The fields mirror the client data of the tree items: the container
     * leaves all of them empty, a preset directory carries its identifier
     * only, an imported folder adds its name, a folder below an import adds
     * its path relative to the import root, and a folder the user created adds
     * its path relative to the preset directory. The path does not record
     * whether a preset directory hangs below another one, because the preset
     * identifiers are unique and a preset is looked up in the whole tree.
     */
    struct TreePath
    {
        /**
         * @brief Identifier of the preset directory, empty for the container.
         */
        std::string preset_id;

        /**
         * @brief Name of the imported folder, empty for preset nodes.
         */
        std::wstring import_name;

        /**
         * @brief Folder path relative to the import root, empty for import
         *        roots.
         */
        std::wstring relative_dir;

        /**
         * @brief Path of a folder the user created, relative to the preset
         *        directory, empty for every other node.
         *
         * A created folder hangs below the layer of its preset directory and
         * not below an import, so its path is looked up from the preset node.
         */
        std::wstring virtual_dir;
    };

    /**
     * @brief Client data attached to one tree item.
     */
    struct TreeNode : public wxTreeItemData
    {
        /**
         * @brief Identifier of the preset directory, empty for the container
         *        item.
         */
        std::string preset_id;

        /**
         * @brief Name of the imported folder, empty for preset nodes.
         */
        std::wstring import_name;

        /**
         * @brief Folder path relative to the import root, empty for import roots.
         */
        std::wstring relative_dir;

        /**
         * @brief Path of a folder the user created, relative to the preset
         *        directory, empty for every other node.
         */
        std::wstring virtual_dir;

        /**
         * @brief Whether the subfolders were already listed.
         */
        bool populated = false;
    };

    /**
     * @brief One row of the file list.
     */
    struct RowInfo
    {
        /**
         * @brief Kind of the row, which decides how Remove behaves.
         */
        enum class Kind
        {
            Preset,         ///< A preset directory of the filesystem tree.
            ImportedFolder, ///< An imported folder below a preset directory.
            VirtualFolder,  ///< A folder the user created inside the view.
            HostEntry,      ///< An entry of the host folder of an import.
            ImportedFile,   ///< A file imported on its own.
            Whiteout        ///< An entry of the host the archive hides.
        };

        /**
         * @brief Kind of the row.
         */
        Kind kind = Kind::HostEntry;

        /**
         * @brief Identifier of the owning preset directory.
         */
        std::string preset_id;

        /**
         * @brief Name of the owning imported folder.
         */
        std::wstring import_name;

        /**
         * @brief Directory of the row relative to the preset directory.
         */
        std::wstring target_dir;

        /**
         * @brief Name shown in the Filename column.
         */
        std::wstring file_name;

        /**
         * @brief Host path of an entry of an imported folder.
         */
        std::wstring host_path;

        /**
         * @brief Host path of an individually imported file.
         */
        std::wstring source_path;

        /**
         * @brief Whether the row describes a folder.
         */
        bool is_directory = false;

        /**
         * @brief Whether the row is a startup file of the model.
         */
        bool is_startup_file = false;
    };

    /**
     * @brief Create the toolbar row above the file list.
     * @param[in] parent Parent window of the row.
     * @return The toolbar row.
     */
    wxWindow* CreateToolBarRow(wxWindow* parent);

    /**
     * @brief Create the report style list of the selected folder.
     * @param[in] parent Parent window of the list.
     */
    void CreateList(wxWindow* parent);

    /**
     * @brief Rebuild the whole tree from the model.
     */
    void BuildTree();

    /**
     * @brief Append one preset directory and everything below it.
     *
     * The item is appended below the given parent, its nested preset
     * directories are appended below it, and its imported folders follow as
     * items of their own. The nested presets come first, so the fixed entries
     * of the tree stay above the content the user added.
     *
     * @param[in] parent Item which holds the preset directory.
     * @param[in] preset Preset directory to append.
     * @return The item of the preset directory.
     */
    wxTreeItemId AppendPreset(const wxTreeItemId& parent, const appbox::PresetDirectory& preset);

    /**
     * @brief Append one folder the user created and mark it as a folder.
     *
     * The folders below the folder are listed on demand, like the host
     * subfolders of an import are, so the item is marked as one which holds
     * children without listing them.
     *
     * @param[in] parent Item which holds the folder.
     * @param[in] preset_id Identifier of the owning preset directory.
     * @param[in] path Path of the folder relative to the preset directory.
     * @return The item of the folder.
     */
    wxTreeItemId AppendVirtualDirectory(const wxTreeItemId& parent, const std::string& preset_id,
                                        const std::wstring& path);

    /**
     * @brief Append the subfolders of one directory node.
     *
     * The host subfolders of the directory are appended first, because they
     * come from the imported folder, and the folders the user created follow.
     * The container and the preset directories are not populated: their
     * children belong to the fixed tree, see AppendPreset().
     *
     * @param[in] item Tree item holding TreeNode data.
     */
    void PopulateNode(const wxTreeItemId& item);

    /**
     * @brief Refresh the file list for the selected tree node.
     */
    void RefreshList();

    /**
     * @brief Fill the rows with the top level preset directories of the container.
     */
    void ListPresets();

    /**
     * @brief Fill the rows with the content below one preset directory.
     *
     * A preset directory holds nested preset directories and the imported
     * folders which were imported below it. The nested presets are listed
     * first, mirroring the order of the tree.
     *
     * @param[in] node Data of the selected preset node.
     */
    void ListPresetImports(const TreeNode& node);

    /**
     * @brief Fill the rows with the content of one folder of an import.
     * @param[in] node Data of the selected import or folder node.
     */
    void ListFolderContent(const TreeNode& node);

    /**
     * @brief Fill the rows with the folders the user created inside a directory.
     *
     * The rows are appended after the rows of the host content and of the
     * imported files, so the order of the list mirrors the order of the tree.
     *
     * @param[in] preset_id Identifier of the owning preset directory.
     * @param[in] target_dir Directory relative to the preset directory, empty
     *            for the layer root.
     */
    void ListVirtualFolders(const std::string& preset_id, const std::wstring& target_dir);

    /**
     * @brief Fill the rows with the content of a folder the user created.
     *
     * Such a folder has no host counterpart, so it holds the folders the user
     * created inside it and the imported files which were imported into it.
     *
     * @param[in] node Data of the selected folder node.
     */
    void ListVirtualFolderContent(const TreeNode& node);

    /**
     * @brief Fill the rows with the entries a directory of the view hides.
     *
     * A hidden entry has no host counterpart of its own: it is an entry of the
     * layer of its preset directory, which the pack run writes as the whiteout
     * marker of the entry. Its row shows the mode `Whiteout` and offers no
     * other mode, because the marker is what hides the entry and not a mode of
     * the isolation table.
     *
     * @param[in] preset_id Identifier of the owning preset directory.
     * @param[in] target_dir Directory relative to the preset directory, empty
     *            for the layer root.
     */
    void ListWhiteouts(const std::string& preset_id, const std::wstring& target_dir);

    /**
     * @brief Fill the rows with the hidden entries which hang below no folder of the view.
     *
     * An entry whose parent directory is not a folder of the view has no list
     * which could show it, so it is listed by the preset directory it belongs
     * to. Its source path still names the place of the entry inside the view.
     *
     * @param[in] preset_id Identifier of the owning preset directory.
     */
    void ListOrphanWhiteouts(const std::string& preset_id);

    /**
     * @brief Rebuild the list from the rows matching the search box.
     */
    void ApplyFilter();

    /**
     * @brief Append one row to the list control.
     * @param[in] row Row description.
     * @param[in] index Index of the row inside the row vector.
     */
    void AppendRow(const RowInfo& row, std::size_t index);

    /**
     * @brief Get the icon shown in the Filename column of one row.
     * @param[in] row Row description.
     * @return The icon of the row: a folder for a directory row, a plain file
     *         for every other row.
     */
    const wxBitmapBundle& IconOf(const RowInfo& row) const;

    /**
     * @brief Get the path of the current tree selection.
     * @param[out] path Path of the selected item.
     * @return true when an item is selected.
     */
    bool SelectedTreePath(TreePath& path) const;

    /**
     * @brief Find the item of a preset directory inside a subtree.
     *
     * The preset directories form a tree, so a preset is looked up in the
     * whole subtree of the given item and not only among its direct children.
     *
     * @param[in] parent Item whose subtree is searched.
     * @param[in] preset_id Identifier of the preset directory.
     * @return The item of the preset directory, an invalid item when the
     *         subtree does not hold it.
     */
    wxTreeItemId FindPresetItem(const wxTreeItemId& parent, const std::string& preset_id) const;

    /**
     * @brief Find the item of a folder the user created inside a subtree.
     *
     * The path of such a folder is stored relative to the preset directory, so
     * the lookup starts at the preset item and walks the levels of the path:
     * every level is listed on demand and is named either by an imported folder
     * or by a folder the user created.
     *
     * @param[in] preset_item Item of the preset directory which owns the folder.
     * @param[in] path Path of the folder relative to the preset directory.
     * @return The item of the folder, an invalid item when the tree does not
     *         hold it.
     */
    wxTreeItemId FindVirtualDirectoryItem(const wxTreeItemId& preset_item, const std::wstring& path);

    /**
     * @brief Select the tree item of a path.
     *
     * The folders below an imported folder are listed on demand, so the levels
     * of the path are opened while the item is looked up. A path which the
     * tree does not hold falls back to the deepest level it was found in, and
     * to the container when the preset directory itself is gone.
     *
     * @param[in] path Path of the item.
     */
    void SelectTreePath(const TreePath& path);

    /**
     * @brief Get the directory of the current selection inside the sandbox view.
     * @param[out] preset_id Identifier of the owning preset directory.
     * @param[out] target_dir Directory relative to the preset directory.
     * @param[out] import_name First segment of the directory, empty when the
     *             directory is the layer root of its preset directory.
     * @return true when the selection names a folder of the view.
     */
    bool SelectedTarget(std::string& preset_id, std::wstring& target_dir, std::wstring& import_name) const;

    /**
     * @brief Get the directory a tree node names, relative to its preset directory.
     *
     * The container and a preset directory name the layer root, which is the
     * empty path; an imported folder and a folder below it name the path below
     * the import root, and a folder the user created names the path it was
     * stored under.
     *
     * @param[in] node Data of the tree node.
     * @return The path relative to the preset directory.
     */
    static std::wstring NodeDirectoryPath(const TreeNode& node);

    /**
     * @brief Get the directory a row of a created folder names.
     *
     * A row stores the directory it lives in, so the folder itself is that
     * directory extended by its name.
     *
     * @param[in] row Row description of a created folder.
     * @return The path of the folder relative to the preset directory.
     */
    static std::wstring DirectoryPathOf(const RowInfo& row);

    /**
     * @brief Get the index of a row inside the row vector.
     * @param[in] item Item of the row.
     * @return The row index, -1 when the item does not describe a row.
     */
    int RowIndex(const wxDataViewItem& item) const;

    /**
     * @brief Get the index of the selected row inside the row vector.
     * @return The row index, -1 when no row is selected.
     */
    int SelectedRowIndex() const;

    /**
     * @brief Select the row which shows one entry of the view.
     *
     * The row is looked up by the virtual path the `Source Path` column shows,
     * because the rows are rebuilt from the model and the indices of a rebuild
     * differ from the ones of the table which was shown before it.
     *
     * @param[in] view_path Virtual path of the entry.
     */
    void SelectRowOfPath(const std::wstring& view_path);

    /**
     * @brief Get the kind of the entry behind a row.
     * @param[in] row Row description.
     * @return The kind of the entry.
     */
    static appbox::FilesystemEntryKind RowKind(const RowInfo& row);

    /**
     * @brief Get the path of a row relative to the root of its imported folder.
     *
     * A startup file of the model is stored relative to the import root, while
     * a row stores its directory relative to the preset directory, whose first
     * segment is the name of the imported folder.
     *
     * @param[in] row Row description.
     * @return The path of the file relative to the import root.
     */
    static std::wstring StartupRelativePath(const RowInfo& row);

    /**
     * @brief Add the selected row to the startup files.
     * @param[in] auto_start Whether the sandbox starts the file by itself.
     */
    void AddSelectedStartupFile(bool auto_start);

    /**
     * @brief Compose the virtual path of a row inside the sandbox view.
     *
     * The path is the one the `Source Path` column shows and the one the
     * isolation mode of the row is stored under: the layer key of the owning
     * preset directory followed by the path of the entry below it.
     *
     * @param[in] row Row description.
     * @return The virtual path, empty when the owning preset is unknown.
     */
    std::wstring RowViewPath(const RowInfo& row) const;

    /**
     * @brief Get the isolation modes a row accepts.
     *
     * The renderer of the isolation column calls this for the row which is
     * about to be edited, so a file is never offered a mode it cannot hold.
     *
     * @param[in] item Item of the row.
     * @return The display names of the modes, empty when the item does not
     *         describe a row.
     */
    wxArrayString IsolationChoices(const wxDataViewItem& item) const;

    /**
     * @brief Store the isolation mode picked for a row.
     * @param[in] row Row description.
     * @param[in] isolation New isolation mode.
     */
    void ApplyIsolation(const RowInfo& row, appbox::FilesystemIsolation isolation);

    /**
     * @brief Compose the virtual path of a tree node.
     *
     * The container is the root of the view, which is the folder every path no
     * other entry covers belongs to, so it carries an empty path. A preset
     * directory carries the layer key of its preset, an imported folder adds
     * its name and a folder below an import adds its path relative to the
     * import root.
     *
     * @param[in] node Data of the tree node.
     * @param[out] view_path The virtual path of the node, empty for the root.
     * @return true when the node names a path of the view.
     */
    bool NodeViewPath(const TreeNode& node, std::wstring& view_path) const;

    /**
     * @brief Set the isolation mode of a tree node through the dialog.
     *
     * The dialog is opened for the node the user picked in the tree. The mode
     * it returns reaches the node itself, and it reaches the folders below the
     * node as well while the recursion of the dialog was chosen.
     *
     * The container is the root of the view and not a folder of it: its mode
     * decides the locations no entry covers, so the call keeps the layers of
     * the view, which are the preset directories, on the mode they show today
     * while the recursion is off.
     *
     * @param[in] node Data of the tree node.
     */
    void EditIsolation(const TreeNode& node);

    /**
     * @brief Update the enabled state of the toolbar buttons.
     */
    void UpdateToolBarState();

    /**
     * @brief Handle a selection change in the tree.
     * @param[in] event Tree event.
     */
    void OnTreeSelectionChanged(wxTreeEvent& event);

    /**
     * @brief Populate a folder node which is about to expand.
     * @param[in] event Tree expanding event.
     */
    void OnTreeItemExpanding(wxTreeEvent& event);

    /**
     * @brief Show the host folder behind the tree item the mouse rests on.
     *
     * The tooltip carries the host folder the item maps to, see
     * PackModel::HostFolderPath(). The container of the filesystem view has no
     * host counterpart and stays without a tooltip, which also clears the text
     * of the item the mouse came from.
     *
     * @param[in] event Tree tooltip event carrying the item to describe.
     */
    void OnTreeItemToolTip(wxTreeEvent& event);

    /**
     * @brief Drop the tree tooltip while the mouse is not over an item.
     *
     * The item tooltip of the native control is a tooltip of the tree control
     * as a whole, so it would stay attached while the mouse rests on a part of
     * the control which carries no item. The handler drops the text as soon as
     * the cursor leaves the items, so a host path is never shown for a blank
     * part of the tree.
     *
     * @param[in] event Mouse motion event of the tree.
     */
    void OnTreeMouseMove(wxMouseEvent& event);

    /**
     * @brief Show the context menu of a tree item.
     *
     * Every item offers the isolation dialog, which reaches the container as
     * well, and the delete command, which is greyed out for the nodes the view
     * may not remove. The command which imports a folder is offered for the
     * items which carry the preset directory it works on.
     *
     * @param[in] event Tree context menu event.
     */
    void OnTreeItemContextMenu(wxTreeEvent& event);

    /**
     * @brief Import individual host files into the selected folder.
     * @param[in] event Command event.
     */
    void OnAddFiles(wxCommandEvent& event);

    /**
     * @brief Import a host folder below the preset of the selection.
     * @param[in] event Command event.
     */
    void OnAddFolder(wxCommandEvent& event);

    /**
     * @brief Create an empty folder inside the folder the tree selects.
     *
     * The folder carries the default name of the directory, see
     * PackModel::FreeVirtualDirectoryName(), and becomes the selected row of
     * the list, so the user sees it and can rename it in place. The container
     * holds the preset directories and is not a folder of the view, so the
     * command is refused for it.
     *
     * @param[in] event Command event.
     */
    void OnNewFolder(wxCommandEvent& event);

    /**
     * @brief Remove the selected import after confirmation.
     * @param[in] event Command event.
     */
    void OnRemove(wxCommandEvent& event);

    /**
     * @brief Remove the folder behind the tree node of the context menu.
     *
     * Only the folders the view may remove are passed here: an imported folder
     * and a folder the user created. The command is refused for every other
     * node, which the context menu greys its item out for.
     *
     * @param[in] event Command event.
     */
    void OnTreeDeleteNode(wxCommandEvent& event);

    /**
     * @brief Set the isolation mode of the selected tree node.
     * @param[in] event Command event.
     */
    void OnTreeIsolation(wxCommandEvent& event);

    /**
     * @brief Move the tree selection to the parent folder.
     * @param[in] event Command event.
     */
    void OnUpDir(wxCommandEvent& event);

    /**
     * @brief Apply the search box content to the file list.
     * @param[in] event Command event.
     */
    void OnSearch(wxCommandEvent& event);

    /**
     * @brief Follow a double click on a row of the file list.
     *
     * A double click on a preset directory enters it by selecting its tree
     * node, which rebuilds the table. That rebuild is deferred to the next
     * event loop iteration, because the table still holds the activated row
     * while it dispatches the activation event.
     *
     * @param[in] event Table item activation event.
     */
    void OnRowActivated(wxDataViewEvent& event);

    /**
     * @brief Decide whether a cell of the file list may be edited in place.
     *
     * Only the name of a folder the user created is edited in the list, so the
     * edit is refused for every other row. The control starts the edit of a
     * cell when the user clicks it a second time without a double click, which
     * is the way a name of the Windows Explorer is renamed as well.
     *
     * @param[in] event Table item start editing event.
     */
    void OnFilenameStartEditing(wxDataViewEvent& event);

    /**
     * @brief Store the name the user typed for a folder of the list.
     *
     * The event carries the typed name while the model of the control still
     * holds the old one, so the name is stored through the pack model and the
     * table is rebuilt afterwards, which restores the old name when the model
     * refuses the new one. The value of the event is not stored in the control,
     * because the row is rebuilt from the model either way.
     *
     * @param[in] event Table item editing done event.
     */
    void OnFilenameEditingDone(wxDataViewEvent& event);

    /**
     * @brief Rename a folder the user created and keep its isolation modes.
     *
     * The pack model renames the folder and its subtree, the isolation model
     * moves the modes which were picked below it to the new path, and the tree
     * and the list are rebuilt. Both models are asked before either of them is
     * changed, so a refusal leaves the workspace as it was.
     *
     * @param[in] row Row description of the folder.
     * @param[in] new_name Name the user typed.
     */
    void RenameVirtualDirectory(const RowInfo& row, const std::wstring& new_name);

    /**
     * @brief Handle a mode picked from the isolation dropdown of a row.
     *
     * The table is rebuilt once the control finished its edit, because a
     * rebuild inside the event would delete the row the control still holds
     * while it commits the value.
     *
     * @param[in] event Table value change event.
     */
    void OnIsolationChanged(wxDataViewEvent& event);

    /**
     * @brief Show the context menu of a row of the file list.
     *
     * Only startable rows offer the startup file commands, because only a
     * program, a batch file, a command script or a PowerShell script can be
     * started by the sandbox.
     *
     * @param[in] event Table item context menu event.
     */
    void OnRowContextMenu(wxDataViewEvent& event);

    /**
     * @brief Add the selected startable file as an auto start startup file.
     * @param[in] event Command event.
     */
    void OnSetStartupFile(wxCommandEvent& event);

    /**
     * @brief Add the selected startable file as a startup file without auto start.
     * @param[in] event Command event.
     */
    void OnAddToStartupFileList(wxCommandEvent& event);

    appbox::PackModel&                model_;
    appbox::FilesystemIsolationModel& isolation_;

    /**
     * @brief Whether the panel is rebuilding itself.
     *
     * The rebuild suppresses the events of the controls which would otherwise
     * be read as input of the user: the value change events of the table,
     * which carry the isolation mode of a row, and the selection change events
     * of the tree, which rebuild the table.
     */
    bool updating_ = false;

    /**
     * @brief Whether the tooltip of the tree carries the host path of an item.
     *
     * The item tooltip of the native control is a tooltip of the tree control
     * as a whole, so the panel tracks whether a host path is attached to it
     * and drops the text as soon as the mouse leaves the items.
     */
    bool item_tooltip_shown_ = false;

    wxTreeCtrl*         tree_ = nullptr;
    wxDataViewListCtrl* list_ = nullptr;
    wxSearchCtrl*       search_ = nullptr;
    wxButton*           add_files_ = nullptr;
    wxButton*           add_folder_ = nullptr;
    wxButton*           new_folder_ = nullptr;
    wxButton*           remove_ = nullptr;
    wxButton*           up_dir_ = nullptr;

    std::vector<RowInfo> rows_;

    /**
     * @brief Icon shown before the name of a folder row of the file list.
     */
    wxBitmapBundle folder_icon_;

    /**
     * @brief Icon shown before the name of a file row of the file list.
     */
    wxBitmapBundle file_icon_;
};

#endif // APPBOX_PACKER_WIDGET_FILESYSTEM_PANEL_HPP
