#ifndef APPBOX_PACKER_WIDGET_DATA_VIEW_TOOLTIP_HPP
#define APPBOX_PACKER_WIDGET_DATA_VIEW_TOOLTIP_HPP

#include <wx/wx.h>
#include <wx/dataview.h>
#include <wx/headerctrl.h>
#include <functional>

/**
 * @brief Show a tooltip while the cursor is over a table of a workspace.
 *
 * A data view column has no tooltip of its own, so the description of the cell
 * or of the column header below the cursor is set on the window which is under
 * the cursor. The rows and the header of a `wxDataViewListCtrl` are child
 * windows of the control, and wxWidgets only propagates command events to the
 * parent window, so the mouse events of a row and of a header never reach the
 * control itself: the helper binds the events of the two child windows instead
 * and translates their positions back into the coordinates of the control.
 *
 * The rows are the client area of the control (`wxDataViewCtrl::GetMainWindow()`)
 * and the header is the header control of the generic implementation
 * (`wxDataViewCtrl::GenericGetHeader()`), whose own child carries the events of
 * the header. A handler which is bound to a window is called before the event
 * table of the window, so the description is resolved even while the window
 * consumes the event itself.
 *
 * A window shows one tooltip and wxWidgets pops it up after the cursor rested on
 * the window for a moment, so the text is only written while it changes and is
 * dropped while the cursor leaves the window.
 */
class DataViewTooltip
{
public:
    /**
     * @brief Description of a cell of the table.
     *
     * @param[in] row Index of the row below the cursor, -1 when the cursor is
     *                not on a row.
     * @param[in] column Model column of the cell, -1 when no column is hit.
     * @return The description of the cell, empty when it has none.
     */
    using CellText = std::function<wxString(int row, int column)>;

    /**
     * @brief Description of a column of the header of the table.
     * @param[in] column Model column of the header, -1 when no column is hit.
     * @return The description of the column, empty when it has none.
     */
    using HeaderText = std::function<wxString(int column)>;

    /**
     * @brief Bind the descriptions to the rows and to the header of a table.
     * @param[in] view The table to describe.
     * @param[in] cell Description of a cell of the table.
     * @param[in] header Description of a column of the header of the table.
     */
    DataViewTooltip(wxDataViewListCtrl& view, CellText cell, HeaderText header);

private:
    /**
     * @brief Find the column of the header which is below a position.
     *
     * The columns are walked in display order and their widths are added up,
     * because the header control of wxWidgets has no hit test of its own.
     *
     * @param[in] x Position inside the header control.
     * @return The model column of the header, -1 when no column is hit.
     */
    int HeaderColumnAt(int x) const;

    /**
     * @brief Show a text as the tooltip of the window which received an event.
     * @param[in] event The mouse event.
     * @param[in] text The description to show, empty to drop the tooltip.
     */
    void Show(const wxMouseEvent& event, const wxString& text);

    /**
     * @brief Drop the tooltip which is currently shown.
     */
    void Clear();

    /**
     * @brief Show the description of the cell below the cursor.
     * @param[in] event Mouse event of the rows of the table.
     */
    void OnRowsMouseMove(wxMouseEvent& event);

    /**
     * @brief Show the description of the column header below the cursor.
     * @param[in] event Mouse event of the header of the table.
     */
    void OnHeaderMouseMove(wxMouseEvent& event);

    /**
     * @brief Drop the description of a window the cursor left.
     * @param[in] event Mouse event of the window which was left.
     */
    void OnLeaveWindow(wxMouseEvent& event);

    wxDataViewListCtrl& view_;
    wxWindow*           rows_ = nullptr;
    wxHeaderCtrl*       header_ = nullptr;
    CellText            cell_text_;
    HeaderText          header_text_;

    /**
     * @brief Window whose tooltip is currently shown, null while none is shown.
     *
     * The tooltip of a window is a single control, so the text is dropped from
     * the window it was set on before another window shows one.
     */
    wxWindow* shown_window_ = nullptr;

    /**
     * @brief Text which is currently shown as the tooltip.
     */
    wxString shown_text_;
};

#endif // APPBOX_PACKER_WIDGET_DATA_VIEW_TOOLTIP_HPP
