#ifndef APPBOX_PACKER_WIDGET_ENVIRONMENT_PANEL_HPP
#define APPBOX_PACKER_WIDGET_ENVIRONMENT_PANEL_HPP

#include <wx/wx.h>
#include <wx/dataview.h>
#include "DataViewTooltip.hpp"
#include "core/EnvironmentModel.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Environment workspace of the packer.
 *
 * The workspace holds a toolbar row with `Add` and `Remove` above the table of
 * the environment variables of the model, whose five columns - `Name`,
 * `Value`, `IsolationMode`, `MergeMode` and `MergeString` - are edited inside
 * the cell: the three text columns are typed, the two mode columns are picked
 * from a dropdown.
 *
 * The table shows one row per stored variable plus at most one row which is
 * still being filled in: `Add` appends such a draft, and the row reaches the
 * model as soon as it carries a name. A value the model refuses is reported and
 * the stored value is put back into the cell.
 *
 * A name which is the search path variable is filled in with the merge mode
 * `Prepend` and the merge string `;` while the name becomes that variable, see
 * `appbox::ApplyPathVariableDefaults()`; the two cells show the values which
 * were stored, and the user may change them afterwards.
 *
 * Every control explains itself with a tooltip: the two buttons carry one, and
 * the table answers the mouse through `DataViewTooltip`, which describes the
 * cell below the cursor - for the two mode columns the meaning of the mode the
 * row holds - and the column of the header below it, which names the modes the
 * column offers.
 */
class EnvironmentPanel : public wxPanel
{
public:
    /**
     * @brief Create the environment workspace.
     * @param[in] parent Parent window.
     * @param[in,out] model The shared environment model.
     */
    EnvironmentPanel(wxWindow* parent, appbox::EnvironmentModel& model);

    /**
     * @brief Rebuild the table from the model.
     *
     * The model is replaced as a whole when a project file is imported, so the
     * rows of the table - including a draft which is still being filled in -
     * are dropped and built from the new content of the model.
     */
    void RefreshModel();

private:
    /**
     * @brief One row of the table.
     */
    struct RowInfo
    {
        /**
         * @brief Name of the variable of the row.
         */
        std::wstring name;

        /**
         * @brief Value of the variable of the row.
         */
        std::wstring value;

        /**
         * @brief Isolation mode of the row.
         */
        appbox::EnvironmentIsolation isolation = appbox::EnvironmentIsolation::WriteCopy;

        /**
         * @brief Merge mode of the row.
         */
        appbox::EnvironmentMergeMode merge = appbox::environment_isolation::kDefaultMergeMode;

        /**
         * @brief Text which joins the two values of the row.
         */
        std::wstring merge_string;

        /**
         * @brief Whether the row is a draft which is not stored yet.
         *
         * A draft row is only held by the table; it reaches the model once it
         * carries a name.
         */
        bool pending = false;
    };

    /**
     * @brief Create the toolbar row above the table.
     * @param[in] parent Parent window of the row.
     * @return The toolbar row.
     */
    wxWindow* CreateToolBarRow(wxWindow* parent);

    /**
     * @brief Create the table of the environment variables.
     * @param[in] parent Parent window of the table.
     */
    void CreateList(wxWindow* parent);

    /**
     * @brief Rebuild the table from the model.
     *
     * The draft row of the table is dropped, because the table is only rebuilt
     * while the model was replaced by a project file.
     */
    void RefreshList();

    /**
     * @brief Append one row to the table.
     * @param[in] row Row description.
     */
    void AppendRow(const RowInfo& row);

    /**
     * @brief Update the enabled state of the toolbar buttons.
     */
    void UpdateToolBarState();

    /**
     * @brief Start editing the name cell of a row.
     *
     * The editor is opened once the event which asked for it was dispatched:
     * the table still holds the row it activated while it dispatches the
     * event, so opening the editor from inside such a handler is not safe.
     *
     * @param[in] row Index of the row to edit.
     */
    void BeginEditRow(int row);

    /**
     * @brief Show the modes which were stored for a row in its cells.
     *
     * The search path rule changes the two mode columns of an entry, and the
     * cells have to show the values which were stored. The write is deferred
     * like every other change of the table, because the event which asked for
     * it is still being dispatched.
     *
     * @param[in] row Index of the row.
     * @param[in] entry The entry which was stored.
     */
    void ShowStoredModes(int row, const appbox::EnvironmentEntry& entry);

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
    void RevertCell(int row, unsigned int column, const wxString& value);

    /**
     * @brief Report a refused value to the user.
     * @param[in] error Error description of the model.
     */
    void ReportError(const std::string& error);

    /**
     * @brief Read the entry the cells of a row hold.
     * @param[in] row Index of the row.
     * @return The entry of the row.
     */
    appbox::EnvironmentEntry EntryOfRow(int row) const;

    /**
     * @brief Describe the cell below the cursor for the tooltip of the table.
     * @param[in] row Index of the row, -1 when the cursor is not on a row.
     * @param[in] column Model column of the cell, -1 when no column is hit.
     * @return The description of the cell, empty when it has none.
     */
    wxString TooltipForCell(int row, int column) const;

    /**
     * @brief Describe a column of the header for the tooltip of the table.
     *
     * The two mode columns list the modes they offer, so the header explains
     * what a mode means without a row of its own.
     *
     * @param[in] column Model column of the header, -1 when no column is hit.
     * @return The description of the column, empty when it has none.
     */
    wxString TooltipForHeader(int column) const;

    /**
     * @brief Append the row of the next environment variable.
     * @param[in] event Command event.
     */
    void OnAdd(wxCommandEvent& event);

    /**
     * @brief Drop the selected environment variable.
     *
     * The row is dropped from the table on its own instead of rebuilding the
     * table, so an editor which another row holds stays untouched.
     *
     * @param[in] event Command event.
     */
    void OnRemove(wxCommandEvent& event);

    /**
     * @brief Store the values the cells of a row were given.
     *
     * A draft row is stored once it carries a name; the values of a stored row
     * replace the entry of the model. A value the model refuses is reported and
     * the cell is put back to the stored value.
     *
     * @param[in] event Table value change event.
     */
    void OnValueChanged(wxDataViewEvent& event);

    appbox::EnvironmentModel& model_;

    /**
     * @brief Whether the table is being rebuilt or a cell is being put back.
     *
     * Both of them suppress the events of the control, which would otherwise
     * be read as a value the user entered.
     */
    bool updating_ = false;

    /**
     * @brief Tooltip of the rows and of the header of the table.
     */
    std::unique_ptr<DataViewTooltip> tooltip_;

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

#endif // APPBOX_PACKER_WIDGET_ENVIRONMENT_PANEL_HPP
