#ifndef APPBOX_PACKER_WIDGET_FILESYSTEM_PANEL_HPP
#define APPBOX_PACKER_WIDGET_FILESYSTEM_PANEL_HPP

#include <wx/wx.h>
#include <wx/dataview.h>
#include <wx/treectrl.h>
#include "core/PackModel.hpp"
#include <string>
#include <vector>

class wxSearchCtrl;

/**
 * @brief Filesystem workspace of the packer.
 *
 * Left side: the tree of the preset directories with their imported folders
 * and the host subfolders of the imports, expanded on demand. Right side: a
 * toolbar row (Add Files, Add Folder, New Folder, Remove, Up Dir and a search
 * box) above the report style list of the selected folder.
 *
 * The list shows the filename, the read only isolation attributes, the size
 * and the virtual source path of every entry, following the layout of the
 * reference packaging tool.
 */
class FilesystemPanel : public wxPanel
{
public:
    /**
     * @brief Create the filesystem workspace.
     * @param[in] parent Parent window.
     * @param[in,out] model The shared pack model.
     */
    FilesystemPanel(wxWindow* parent, appbox::PackModel& model);

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
         * @brief Identifier of the preset directory.
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
     * @brief Get the index of the selected row inside the row vector.
     * @return The row index, -1 when no row is selected.
     */
    int SelectedRowIndex() const;

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

    appbox::PackModel& model_;

    wxTreeCtrl*    tree_ = nullptr;
    wxDataViewListCtrl* list_ = nullptr;
    wxSearchCtrl*  search_ = nullptr;
    wxButton*      add_files_ = nullptr;
    wxButton*      add_folder_ = nullptr;
    wxButton*      remove_ = nullptr;
    wxButton*      up_dir_ = nullptr;

    std::vector<RowInfo> rows_;
};

#endif // APPBOX_PACKER_WIDGET_FILESYSTEM_PANEL_HPP
