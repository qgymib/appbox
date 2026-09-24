#ifndef APPBOX_PACKER_WIDGET_NETWORK_PANEL_HPP
#define APPBOX_PACKER_WIDGET_NETWORK_PANEL_HPP

#include <wx/wx.h>
#include <wx/dataview.h>
#include "core/NetworkModel.hpp"
#include <cstddef>
#include <string>
#include <vector>

class NetworkTabBar;
class wxSimplebook;

/**
 * @brief Network workspace of the packer.
 *
 * The workspace opens on a flat tab strip with the pages `Proxy`, `DNS` and
 * `IP Restrictions`. The `DNS` page is implemented: a toolbar row with
 * `Add...` and `Remove` above the table of the DNS redirections of the model,
 * whose two columns - `Hostname or IP Address` and `Redirect` - are edited
 * inside the cell. The other two pages show the empty state of a reserved
 * isolation domain.
 *
 * The table shows one row per stored redirection plus at most one row which is
 * still being filled in: `Add...` appends such a draft, and the row reaches
 * the model as soon as both of its fields carry a value. A value the model
 * refuses is reported and the stored value is put back into the cell.
 */
class NetworkPanel : public wxPanel
{
public:
    /**
     * @brief Create the network workspace.
     * @param[in] parent Parent window.
     * @param[in,out] model The shared network model.
     */
    NetworkPanel(wxWindow* parent, appbox::NetworkModel& model);

private:
    /**
     * @brief One row of the DNS table.
     */
    struct RowInfo
    {
        /**
         * @brief Hostname or IP address of the row.
         */
        std::wstring hostname;

        /**
         * @brief Redirect target of the row.
         */
        std::wstring redirect;

        /**
         * @brief Whether the row is a draft which is not stored yet.
         *
         * A draft row is only held by the table; it reaches the model once
         * both of its fields carry a value.
         */
        bool pending = false;
    };

    /**
     * @brief Create the page of the DNS redirections.
     * @param[in] parent Parent window of the page.
     * @return The page.
     */
    wxWindow* CreateDnsPage(wxWindow* parent);

    /**
     * @brief Create the toolbar row above the DNS table.
     * @param[in] parent Parent window of the row.
     * @return The toolbar row.
     */
    wxWindow* CreateToolBarRow(wxWindow* parent);

    /**
     * @brief Create the table of the DNS redirections.
     * @param[in] parent Parent window of the table.
     */
    void CreateList(wxWindow* parent);

    /**
     * @brief Rebuild the table from the model and the pending draft.
     *
     * The pending draft, if the table holds one, is kept at the end of the
     * table, so a rebuild never drops a row the user is filling in.
     */
    void RefreshList();

    /**
     * @brief Append one row to the table.
     * @param[in] row Row description.
     */
    void AppendRow(const RowInfo& row);

    /**
     * @brief Put a stored value back into a cell.
     *
     * The change of the store is suppressed as an edit of the user, because
     * the control reports the value it was given back as a change of its own.
     *
     * @param[in] row Index of the row.
     * @param[in] column Model column of the cell.
     * @param[in] value Stored value of the cell.
     */
    void RevertCell(int row, unsigned int column, const std::wstring& value);

    /**
     * @brief Update the enabled state of the toolbar buttons.
     */
    void UpdateToolBarState();

    /**
     * @brief Start editing the hostname cell of a row.
     *
     * The editor is opened once the event which asked for it was dispatched:
     * the table still holds the row it activated while it dispatches the
     * event, so opening the editor from inside such a handler is not safe.
     *
     * @param[in] row Index of the row to edit.
     */
    void BeginEditRow(int row);

    /**
     * @brief Report a refused value to the user.
     * @param[in] error Error description of the model.
     */
    void ReportError(const std::string& error);

    /**
     * @brief Show the page of the activated tab.
     * @param[in] event Command event carrying the tab index.
     */
    void OnTabChanged(wxCommandEvent& event);

    /**
     * @brief Append the row of the next DNS redirection.
     * @param[in] event Command event.
     */
    void OnAdd(wxCommandEvent& event);

    /**
     * @brief Drop the selected DNS redirection.
     *
     * The row is dropped from the table on its own instead of rebuilding the
     * table, so an editor which another row holds stays untouched.
     *
     * @param[in] event Command event.
     */
    void OnRemove(wxCommandEvent& event);

    /**
     * @brief Store the value a cell was given.
     *
     * A draft row is stored once both of its fields carry a value; the value
     * of a stored row replaces the entry of the model. A value the model
     * refuses is reported and the cell is put back to the stored value.
     *
     * @param[in] event Table value change event.
     */
    void OnValueChanged(wxDataViewEvent& event);

    appbox::NetworkModel& model_;

    /**
     * @brief Whether the table is being rebuilt or a cell is being put back.
     *
     * Both suppress the value change events of the control, which would
     * otherwise be read as a value the user entered.
     */
    bool updating_ = false;

    NetworkTabBar*      tab_bar_ = nullptr;
    wxSimplebook*       pages_ = nullptr;
    wxDataViewListCtrl* list_ = nullptr;
    wxButton*           add_ = nullptr;
    wxButton*           remove_ = nullptr;

    /**
     * @brief The rows of the table, in table order.
     *
     * The stored rows are the entries of the model in model order, followed by
     * the pending draft, if the table holds one.
     */
    std::vector<RowInfo> rows_;
};

#endif // APPBOX_PACKER_WIDGET_NETWORK_PANEL_HPP
