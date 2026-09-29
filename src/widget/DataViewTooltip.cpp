#include "DataViewTooltip.hpp"
#include <utility>

namespace
{

/**
 * @brief Get the window which received a mouse event.
 * @param[in] event The mouse event.
 * @return The window of the event, null when the event carries none.
 */
wxWindow* EventWindow(const wxMouseEvent& event)
{
    return wxDynamicCast(event.GetEventObject(), wxWindow);
}

/**
 * @brief Translate a position of a child window into the client area of a table.
 *
 * A cell is looked up with the coordinates of the control itself, while the
 * event carries the coordinates of the child window which received it.
 *
 * @param[in] view The table.
 * @param[in] event The mouse event.
 * @return The position inside the client area of the table.
 */
wxPoint ToControl(wxDataViewListCtrl& view, const wxMouseEvent& event)
{
    wxWindow* window = EventWindow(event);
    if (window == nullptr)
    {
        return event.GetPosition();
    }

    return view.ScreenToClient(window->ClientToScreen(event.GetPosition()));
}

} // namespace

DataViewTooltip::DataViewTooltip(wxDataViewListCtrl& view, CellText cell, HeaderText header)
    : view_(view), cell_text_(std::move(cell)), header_text_(std::move(header))
{
    /*
     * The rows live in the client area of the control, which is a child window
     * of it: the mouse events of a row are sent to that window and never reach
     * the control.
     */
    rows_ = view_.GetMainWindow();
    if (rows_ != nullptr)
    {
        rows_->Bind(wxEVT_MOTION, &DataViewTooltip::OnRowsMouseMove, this);
        rows_->Bind(wxEVT_LEAVE_WINDOW, &DataViewTooltip::OnLeaveWindow, this);
    }

    /*
     * The header is a composite window which holds the real header control, so
     * both of them are bound: the control of the generic implementation sends
     * the events of the header, while a port which draws the header itself
     * sends them to the composite window.
     */
    header_ = view_.GenericGetHeader();
    if (header_ != nullptr)
    {
        header_->Bind(wxEVT_MOTION, &DataViewTooltip::OnHeaderMouseMove, this);
        header_->Bind(wxEVT_LEAVE_WINDOW, &DataViewTooltip::OnLeaveWindow, this);

        for (wxWindow* part : header_->GetChildren())
        {
            part->Bind(wxEVT_MOTION, &DataViewTooltip::OnHeaderMouseMove, this);
            part->Bind(wxEVT_LEAVE_WINDOW, &DataViewTooltip::OnLeaveWindow, this);
        }
    }
}

int DataViewTooltip::HeaderColumnAt(int x) const
{
    if (header_ == nullptr || x < 0)
    {
        return -1;
    }

    int                left = 0;
    const unsigned int count = view_.GetColumnCount();
    for (unsigned int position = 0; position < count; ++position)
    {
        const unsigned int index = header_->GetColumnAt(position);
        wxDataViewColumn*  column = view_.GetColumn(index);
        if (column == nullptr || column->IsHidden())
        {
            continue;
        }

        const int width = column->GetWidth();
        if (x >= left && x < left + width)
        {
            return static_cast<int>(column->GetModelColumn());
        }
        left += width;
    }

    return -1;
}

void DataViewTooltip::OnRowsMouseMove(wxMouseEvent& event)
{
    wxDataViewItem    item;
    wxDataViewColumn* column = nullptr;
    view_.HitTest(ToControl(view_, event), item, column);

    const int row = item.IsOk() ? view_.ItemToRow(item) : -1;
    const int model_column = column != nullptr ? static_cast<int>(column->GetModelColumn()) : -1;

    Show(event, cell_text_(row, model_column));
    event.Skip();
}

void DataViewTooltip::OnHeaderMouseMove(wxMouseEvent& event)
{
    Show(event, header_text_(HeaderColumnAt(event.GetPosition().x)));
    event.Skip();
}

void DataViewTooltip::OnLeaveWindow(wxMouseEvent& event)
{
    Clear();
    event.Skip();
}

void DataViewTooltip::Show(const wxMouseEvent& event, const wxString& text)
{
    wxWindow* window = EventWindow(event);
    if (window == nullptr)
    {
        return;
    }

    /*
     * The tooltip of a window is one control and wxWidgets shows it after the
     * cursor rested on the window, so a text which is already shown is left in
     * place: writing it again would restart the delay of the tooltip.
     */
    if (window == shown_window_ && text == shown_text_)
    {
        return;
    }

    Clear();
    if (text.empty())
    {
        return;
    }

    window->SetToolTip(text);
    shown_window_ = window;
    shown_text_ = text;
}

void DataViewTooltip::Clear()
{
    if (shown_window_ == nullptr)
    {
        return;
    }

    shown_window_->UnsetToolTip();
    shown_window_ = nullptr;
    shown_text_.clear();
}
