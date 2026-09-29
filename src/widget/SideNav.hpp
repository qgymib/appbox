#ifndef APPBOX_PACKER_WIDGET_SIDE_NAV_HPP
#define APPBOX_PACKER_WIDGET_SIDE_NAV_HPP

#include <wx/wx.h>
#include <vector>

/**
 * @brief Event raised when the user activates another navigation item.
 *
 * The event identifier carries the zero based index of the activated item.
 */
wxDECLARE_EVENT(APPBOX_SIDE_NAV, wxCommandEvent);

/**
 * @brief Vertical icon navigation of the packer workspace.
 *
 * The control draws one row per item, starting at the top edge of the client
 * area. The selected row uses the accent background with a bar on its left
 * edge, which is the navigation style of the reference layout. Items are
 * painted by the control itself, so no child windows are involved.
 */
class SideNav : public wxPanel
{
public:
    /**
     * @brief Create the navigation control.
     * @param[in] parent Parent window.
     * @param[in] id Window identifier.
     */
    SideNav(wxWindow* parent, wxWindowID id);

    /**
     * @brief Append one navigation item.
     * @param[in] label Item label.
     * @param[in] art Art provider identifier of the item icon.
     * @param[in] tooltip Text which describes the item, empty for an item
     *                    without a tooltip.
     */
    void AddItem(const wxString& label, const wxString& art, const wxString& tooltip = wxEmptyString);

private:
    /**
     * @brief One navigation item.
     */
    struct Item
    {
        wxString label;   ///< Label of the item.
        wxBitmap icon;    ///< Icon of the item.
        wxString tooltip; ///< Text which describes the item, may be empty.
    };

    /**
     * @brief Find the item below a client position.
     * @param[in] position Client position.
     * @return The item index, wxNOT_FOUND when no item is hit.
     */
    int HitTest(const wxPoint& position) const;

    /**
     * @brief Draw the caption and the items.
     * @param[in] event Paint event.
     */
    void OnPaint(wxPaintEvent& event);

    /**
     * @brief Activate the item below the cursor.
     * @param[in] event Mouse event.
     */
    void OnLeftDown(wxMouseEvent& event);

    /**
     * @brief Track the hovered item and show its tooltip.
     * @param[in] event Mouse event.
     */
    void OnMotion(wxMouseEvent& event);

    /**
     * @brief Drop the hovered item.
     * @param[in] event Mouse event.
     */
    void OnLeaveWindow(wxMouseEvent& event);

    /**
     * @brief Select an item and notify the parent window.
     * @param[in] index Item index.
     */
    void Select(int index);

    std::vector<Item> items_;
    std::size_t       selection_ = 0;
    int               hovered_ = wxNOT_FOUND;

    /**
     * @brief Tooltip the control currently shows.
     *
     * The tooltip of a window is a single control which covers the whole
     * control, so the text is only replaced while the hovered item changes and
     * is dropped while the cursor leaves the navigation.
     */
    wxString tooltip_text_;
};

#endif // APPBOX_PACKER_WIDGET_SIDE_NAV_HPP
