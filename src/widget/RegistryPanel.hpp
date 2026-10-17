#ifndef APPBOX_PACKER_WIDGET_REGISTRY_PANEL_HPP
#define APPBOX_PACKER_WIDGET_REGISTRY_PANEL_HPP

#include <wx/wx.h>
#include <wx/dataview.h>
#include <wx/treectrl.h>
#include "core/RegistryModel.hpp"
#include <cstdint>
#include <string>
#include <vector>

class wxButton;
class wxSearchCtrl;

/**
 * @brief Registry workspace of the packer.
 *
 * Left side: the tree of the virtual registry. Its top item is the
 * `Sandbox Registry` container with the five root keys, which are always
 * shown even when no `.reg` file was imported. Right side: a toolbar row
 * (Add key, Add value, Remove, Up Dir and a search box) above the table of the
 * child view of the selected key.
 *
 * The search box filters the table by the name it shows: a key and a value
 * stay visible while the name column contains the typed text, ignoring the
 * case, and the default value of a key is found through the label the column
 * shows for it. The filter also reaches the container, whose rows are the five
 * root keys.
 *
 * The table shows the sub keys and the values of the selected key with the
 * columns Name, Isolation, Type and Value. The isolation mode of a row is picked
 * from a popup which the `Isolation` column opens when the cell is activated; the
 * popup offers the modes which fit the kind of the row, because a key and a value
 * carry different modes. A mode chosen in the table changes the row alone; every
 * other column is edited by double clicking the row, which opens the key or value
 * dialog, and both dialogs pick the isolation mode of the entry as well. The
 * context menu of the tree offers `Isolation Mode...`, which applies a key mode
 * to the key alone or to its whole subtree, as the dialog asks for, and the
 * `Delete Key` command, which removes a key and everything below it and is greyed
 * out for the fixed entries of the view. The
 * container carries no mode of its own, so the command is offered for it as
 * well and its mode reaches the five preset root keys, or the whole tree when
 * the dialog asks for the subtree.
 *
 * The `Up Dir` command moves the tree selection to the key above the selected
 * one. The container is the root of the tree and the only node without a parent,
 * so the command is offered for every node but the container: a root key moves to
 * the container like a sub key moves to the key it hangs below.
 *
 * The meaning of the modes is not explained by the table: the column and its
 * popup name the mode of a row alone, and every dialog which picks a mode spells
 * the modes out below their names (see `ModeChoiceGroup`).
 */
class RegistryPanel : public wxPanel
{
public:
    /**
     * @brief Create the registry workspace.
     * @param[in] parent Parent window.
     * @param[in,out] model The shared registry model.
     */
    RegistryPanel(wxWindow* parent, appbox::RegistryModel& model);

    /**
     * @brief Rebuild tree and table after the model changed externally.
     */
    void RefreshModel();

    /**
     * @brief Offer the isolation modes of a row and apply the chosen one.
     *
     * The renderer of the `Isolation` column calls this when a cell of the
     * column is activated. The modes a row offers depend on its kind: a key row
     * offers the modes of a key and a value row the modes of a value.
     *
     * @param[in] item Table item of the row.
     * @param[in] cell Rectangle of the cell, in client coordinates.
     * @return true when a mode was chosen and applied.
     */
    bool PopupIsolationMenu(const wxDataViewItem& item, const wxRect& cell);

private:
    /**
     * @brief Client data attached to one tree item.
     */
    struct TreeNode : public wxTreeItemData
    {
        /**
         * @brief Path of the key, empty for the container item.
         */
        std::wstring path;
    };

    /**
     * @brief One row of the child view table.
     */
    struct RowInfo
    {
        /**
         * @brief Kind of the row, which decides how the row is edited.
         */
        enum class Kind
        {
            Key,  ///< A sub key of the displayed key.
            Value ///< A value of the displayed key.
        };

        /**
         * @brief Kind of the row.
         */
        Kind kind = Kind::Key;

        /**
         * @brief Name of the sub key, or the name of the value.
         */
        std::wstring name;

        /**
         * @brief Isolation mode of the key behind a key row.
         *
         * A value carries the two value modes alone, so the field which matches
         * the kind of the row is the one the table shows and edits.
         */
        appbox::RegistryKeyIsolation key_isolation = appbox::RegistryKeyIsolation::Merge;

        /**
         * @brief Isolation mode of the value behind a value row.
         */
        appbox::RegistryValueIsolation value_isolation = appbox::RegistryValueIsolation::Full;

        /**
         * @brief Type of the value, only meaningful for value rows.
         */
        appbox::RegistryValueType type = appbox::RegistryValueType::String;

        /**
         * @brief Raw data of the value, empty for key rows.
         */
        std::vector<std::uint8_t> data;
    };

    /**
     * @brief Create the toolbar row above the table.
     * @param[in] parent Parent window of the row.
     * @return The toolbar row.
     */
    wxWindow* CreateToolBarRow(wxWindow* parent);

    /**
     * @brief Create the table of the child view.
     * @param[in] parent Parent window of the table.
     */
    void CreateList(wxWindow* parent);

    /**
     * @brief Rebuild the whole tree from the model.
     */
    void BuildTree();

    /**
     * @brief Append the sub keys of one key to the tree.
     * @param[in] parent_item Tree item of the parent key.
     * @param[in] parent_path Path of the parent key, empty for the container.
     * @param[in] key Key whose sub keys are appended.
     */
    void AddKeyNodes(const wxTreeItemId& parent_item, const std::wstring& parent_path,
                     const appbox::RegistryKeyNode& key);

    /**
     * @brief Find the tree item of a key path.
     * @param[in] parent_item Item to search below.
     * @param[in] path Path of the key to find.
     * @return The tree item, invalid when the key has no item.
     */
    wxTreeItemId FindTreeItem(const wxTreeItemId& parent_item, const std::wstring& path) const;

    /**
     * @brief Select the tree item of a key path.
     *
     * The ancestors of the item are expanded; a path without an item selects
     * the container.
     *
     * @param[in] path Path of the key to select.
     */
    void SelectTreePath(const std::wstring& path);

    /**
     * @brief Get the path of the selected tree item.
     * @return The path, empty for the container.
     */
    std::wstring SelectedTreePath() const;

    /**
     * @brief Refresh the table for the selected tree item.
     */
    void RefreshList();

    /**
     * @brief Rebuild the table from the rows matching the search box.
     */
    void ApplyFilter();

    /**
     * @brief Append one row to the table.
     * @param[in] row Row description.
     * @param[in] index Index of the row inside the row vector.
     */
    void AppendRow(const RowInfo& row, std::size_t index);

    /**
     * @brief Get the icon shown in the Name column of one row.
     * @param[in] row Row description.
     * @return The icon of the row: a folder for a sub key row, a plain file
     *         for a value row.
     */
    const wxBitmapBundle& IconOf(const RowInfo& row) const;

    /**
     * @brief Update the enabled state of the toolbar buttons.
     */
    void UpdateToolBarState();

    /**
     * @brief Get the row index of a table item.
     * @param[in] item Table item.
     * @return The row index, -1 when the item carries none.
     */
    int RowIndex(const wxDataViewItem& item) const;

    /**
     * @brief Get the index of the selected row inside the row vector.
     * @return The row index, -1 when no row is selected.
     */
    int SelectedRowIndex() const;

    /**
     * @brief Apply an isolation mode chosen in the table to a key row.
     *
     * The mode reaches the row itself and nothing else; a whole subtree is only
     * changed through EditIsolation().
     *
     * @param[in] row Row whose isolation was changed.
     * @param[in] isolation New isolation mode.
     */
    void ApplyIsolation(const RowInfo& row, appbox::RegistryKeyIsolation isolation);

    /**
     * @brief Apply an isolation mode chosen in the table to a value row.
     * @param[in] row Row whose isolation was changed.
     * @param[in] isolation New isolation mode.
     */
    void ApplyIsolation(const RowInfo& row, appbox::RegistryValueIsolation isolation);

    /**
     * @brief Open the isolation dialog of a key.
     *
     * The dialog decides whether the mode reaches the key alone or its whole
     * subtree; the tree and the table are rebuilt afterwards. An empty path
     * names the container of the tree, whose mode reaches the five root keys
     * alone or the whole tree, because the container carries no mode of its
     * own.
     *
     * @param[in] path Path of the key, empty for the container.
     */
    void EditIsolation(const std::wstring& path);

    /**
     * @brief Remove a key and everything below it after confirmation.
     *
     * The command is offered by the context menu of the tree and is refused for
     * the container and for the five root keys, which are the fixed entries of
     * the view and report themselves as not removable. The table shows the
     * parent of the removed key afterwards, which still exists.
     *
     * @param[in] path Path of the key, empty for the container.
     */
    void DeleteKey(const std::wstring& path);

    /**
     * @brief Open the key dialog of a sub key row.
     * @param[in] index Index of the row inside the row vector.
     */
    void EditKey(int index);

    /**
     * @brief Open the value dialog of a value row.
     * @param[in] index Index of the row inside the row vector.
     */
    void EditValue(int index);

    /**
     * @brief Follow a selection change in the tree.
     * @param[in] event Tree event.
     */
    void OnTreeSelectionChanged(wxTreeEvent& event);

    /**
     * @brief Open the context menu of the key under the cursor.
     * @param[in] event Tree event of the right click.
     */
    void OnTreeRightClick(wxTreeEvent& event);

    /**
     * @brief Edit the activated row with the matching dialog.
     * @param[in] event Table item activation event.
     */
    void OnItemActivated(wxDataViewEvent& event);

    /**
     * @brief Add a sub key to the selected key.
     * @param[in] event Command event.
     */
    void OnAddKey(wxCommandEvent& event);

    /**
     * @brief Add a value to the selected key.
     * @param[in] event Command event.
     */
    void OnAddValue(wxCommandEvent& event);

    /**
     * @brief Remove the selected row after confirmation.
     * @param[in] event Command event.
     */
    void OnRemove(wxCommandEvent& event);

    /**
     * @brief Move the tree selection to the parent key.
     * @param[in] event Command event.
     */
    void OnUpDir(wxCommandEvent& event);

    /**
     * @brief Apply the search box content to the table.
     * @param[in] event Command event.
     */
    void OnSearch(wxCommandEvent& event);

    appbox::RegistryModel& model_;

    wxTreeCtrl*         tree_ = nullptr;
    wxDataViewListCtrl* list_ = nullptr;
    wxSearchCtrl*       search_ = nullptr;
    wxButton*           add_key_ = nullptr;
    wxButton*           add_value_ = nullptr;
    wxButton*           remove_ = nullptr;
    wxButton*           up_dir_ = nullptr;

    /**
     * @brief Icon shown before the name of a sub key row of the table.
     */
    wxBitmapBundle folder_icon_;

    /**
     * @brief Icon shown before the name of a value row of the table.
     */
    wxBitmapBundle file_icon_;

    /**
     * @brief Rows shown by the table, indexed by the item data of the control.
     */
    std::vector<RowInfo> rows_;

    /**
     * @brief Path of the key the table shows, empty for the container.
     */
    std::wstring current_path_;

    /**
     * @brief Whether the views are rebuilt, which suppresses their events.
     */
    bool updating_ = false;
};

#endif // APPBOX_PACKER_WIDGET_REGISTRY_PANEL_HPP
