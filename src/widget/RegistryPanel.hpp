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

/**
 * @brief Registry workspace of the packer.
 *
 * Left side: the tree of the virtual registry. Its top item is the
 * `Sandbox Registry` container with the five root keys, which are always
 * shown even when no `.reg` file was imported. Right side: a toolbar row
 * (Add value, Add key, Remove) above the table of the child view of the
 * selected key.
 *
 * The table shows the sub keys and the values of the selected key with the
 * columns Name, Isolation, Type and Value. The isolation mode of every row is
 * edited through a dropdown in the row itself, which changes the row alone;
 * every other column is edited by double clicking the row, which opens the key
 * or value dialog. The context menu of the tree offers `Isolation Mode...`,
 * which applies a mode to a whole subtree when the dialog asks for it.
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
            Key,   ///< A sub key of the displayed key.
            Value  ///< A value of the displayed key.
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
         * @brief Isolation mode of the entry behind the row.
         */
        appbox::RegistryIsolation isolation = appbox::RegistryIsolation::WriteCopy;

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
     * @brief Append one row to the table.
     * @param[in] row Row description.
     * @param[in] index Index of the row inside the row vector.
     */
    void AppendRow(const RowInfo& row, std::size_t index);

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
     * @brief Apply an isolation mode chosen in the table.
     *
     * The mode reaches the row itself and nothing else; a whole subtree is only
     * changed through EditIsolation().
     *
     * @param[in] row Row whose isolation was changed.
     * @param[in] isolation New isolation mode.
     */
    void ApplyIsolation(const RowInfo& row, appbox::RegistryIsolation isolation);

    /**
     * @brief Open the isolation dialog of a key.
     *
     * The dialog decides whether the mode reaches the key alone or its whole
     * subtree; the tree and the table are rebuilt afterwards.
     *
     * @param[in] path Path of the key.
     */
    void EditIsolation(const std::wstring& path);

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
     * @brief Apply an isolation mode chosen in the isolation column.
     * @param[in] event Table item value change event.
     */
    void OnIsolationChanged(wxDataViewEvent& event);

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

    appbox::RegistryModel& model_;

    wxTreeCtrl*         tree_ = nullptr;
    wxDataViewListCtrl* list_ = nullptr;
    wxButton*           add_value_ = nullptr;
    wxButton*           add_key_ = nullptr;
    wxButton*           remove_ = nullptr;

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
