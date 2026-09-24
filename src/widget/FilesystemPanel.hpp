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
 * `Sandbox Filesystem` container with the preset directories below it, which
 * hold their imported folders and the host subfolders of the imports,
 * expanded on demand. Right side: a toolbar row (Add Files, Add Folder, New
 * Folder, Remove, Up Dir and a search box) above the report style list of the
 * selected folder.
 *
 * The list shows the filename, the isolation mode, the size and the virtual
 * source path of every entry, following the layout of the reference packaging
 * tool. The mode of a row is picked from a dropdown in the row itself: a
 * folder offers `Full`, `Write Copy` and `Whiteout`, a file offers `Full` and
 * `Whiteout`. A row which the user never touched shows the mode it inherits
 * from the closest folder above it, so the mode of a folder reaches the
 * entries below it. The container lists the preset directories themselves;
 * they are fixed, so they can neither be removed nor renamed.
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
    FilesystemPanel(wxWindow* parent, appbox::PackModel& model,
                    appbox::FilesystemIsolationModel& isolation);

    /**
     * @brief Rebuild tree and list after the model changed externally.
     */
    void RefreshModel();

private:
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
            Preset,         ///< A preset directory below the filesystem container.
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
         * @brief Whether the row is the selected main program.
         */
        bool is_main_program = false;
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
     * @brief Append the host subfolders of one import node.
     * @param[in] item Tree item holding TreeNode data.
     */
    void PopulateNode(const wxTreeItemId& item);

    /**
     * @brief Refresh the file list for the selected tree node.
     */
    void RefreshList();

    /**
     * @brief Fill the rows with the preset directories held by the container.
     */
    void ListPresets();

    /**
     * @brief Fill the rows with the imports below one preset directory.
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
     * @brief Select one tree node by its preset and import.
     * @param[in] preset_id Identifier of the preset directory.
     * @param[in] import_name Name of the imported folder, empty for presets.
     */
    void SelectNode(const std::string& preset_id, const std::wstring& import_name);

    /**
     * @brief Get the directory of the current selection inside the sandbox view.
     * @param[out] preset_id Identifier of the owning preset directory.
     * @param[out] target_dir Directory relative to the preset directory.
     * @param[out] import_name Name of the owning imported folder.
     * @return true when the selection is inside an imported folder.
     */
    bool SelectedTarget(std::string& preset_id, std::wstring& target_dir,
                        std::wstring& import_name) const;

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
     * @brief Show the context menu of a tree item.
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

    appbox::PackModel&                model_;
    appbox::FilesystemIsolationModel& isolation_;

    /**
     * @brief Whether the table is being rebuilt.
     *
     * The rebuild suppresses the value change events of the control, which
     * would otherwise be read as a mode the user picked.
     */
    bool updating_ = false;

    wxTreeCtrl*    tree_ = nullptr;
    wxDataViewListCtrl* list_ = nullptr;
    wxSearchCtrl*  search_ = nullptr;
    wxButton*      add_files_ = nullptr;
    wxButton*      add_folder_ = nullptr;
    wxButton*      remove_ = nullptr;
    wxButton*      up_dir_ = nullptr;

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
