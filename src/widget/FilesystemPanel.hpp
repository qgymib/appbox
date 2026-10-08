#ifndef APPBOX_PACKER_WIDGET_FILESYSTEM_PANEL_HPP
#define APPBOX_PACKER_WIDGET_FILESYSTEM_PANEL_HPP

#include <wx/wx.h>
#include <wx/dataview.h>
#include <wx/treectrl.h>
#include "DataViewTooltip.hpp"
#include "core/FilesystemIsolationModel.hpp"
#include "core/PackModel.hpp"
#include <memory>
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
 * directory holds its imported folders and the host subfolders of the
 * imports, expanded on demand. Right side: a toolbar row (Add Files, Add
 * Folder, New Folder, Remove, Up Dir and a search box) above the report style
 * list of the selected folder.
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
 * The isolation mode of a folder is picked from the context menu of the tree,
 * which reaches every node of the view: the dialog it opens offers the modes a
 * folder accepts and the option to apply the chosen mode to the subfolders as
 * well. The container is the root of the view, so the mode picked for it
 * decides every path no other entry covers, including the locations outside
 * the virtual filesystem.
 *
 * The header of the `Isolation` column explains the modes the column offers,
 * see `DataViewTooltip`; the cells of the list carry no tooltip of their own.
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
     * only, an imported folder adds its name, and a folder below an import
     * adds its path relative to the import root. The path does not record
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
         * @brief Whether the host subfolders were already listed.
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
            HostEntry,      ///< An entry of the host folder of an import.
            ImportedFile    ///< A file imported on its own.
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
     * @brief Append the host subfolders of one import node.
     * @param[in] item Tree item holding TreeNode data.
     */
    void PopulateNode(const wxTreeItemId& item);

    /**
     * @brief Describe a column of the header for the tooltip of the list.
     *
     * The `Isolation` column lists the modes it offers, so the header explains
     * what a mode means without a row of its own.
     *
     * @param[in] column Model column of the header, -1 when no column is hit.
     * @return The description of the column, empty when it has none.
     */
    wxString TooltipForHeader(int column) const;

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
     * @param[out] import_name Name of the owning imported folder.
     * @return true when the selection is inside an imported folder.
     */
    bool SelectedTarget(std::string& preset_id, std::wstring& target_dir, std::wstring& import_name) const;

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
     * well. The commands which import a folder or remove an import are offered
     * for the items which carry the preset directory or the import they work
     * on.
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
     * @brief Remove the selected import after confirmation.
     * @param[in] event Command event.
     */
    void OnRemove(wxCommandEvent& event);

    /**
     * @brief Remove the imported folder of the tree context menu.
     * @param[in] event Command event.
     */
    void OnRemoveImportFromTree(wxCommandEvent& event);

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
     * Only executable rows offer the startup file commands, because only an
     * executable can be started by the sandbox.
     *
     * @param[in] event Table item context menu event.
     */
    void OnRowContextMenu(wxDataViewEvent& event);

    /**
     * @brief Add the selected executable as an auto start startup file.
     * @param[in] event Command event.
     */
    void OnSetStartupFile(wxCommandEvent& event);

    /**
     * @brief Add the selected executable as a startup file without auto start.
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

    /**
     * @brief Tooltip of the rows and of the header of the file list.
     */
    std::unique_ptr<DataViewTooltip> tooltip_;

    wxTreeCtrl*         tree_ = nullptr;
    wxDataViewListCtrl* list_ = nullptr;
    wxSearchCtrl*       search_ = nullptr;
    wxButton*           add_files_ = nullptr;
    wxButton*           add_folder_ = nullptr;
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
