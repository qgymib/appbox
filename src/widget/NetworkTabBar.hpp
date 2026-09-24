#ifndef APPBOX_PACKER_WIDGET_NETWORK_TAB_BAR_HPP
#define APPBOX_PACKER_WIDGET_NETWORK_TAB_BAR_HPP

#include <wx/wx.h>
#include <vector>

/**
 * @brief Event raised when the user activates another tab of the tab bar.
 *
 * The event identifier carries the zero based index of the activated tab.
 */
wxDECLARE_EVENT(APPBOX_NETWORK_TAB, wxCommandEvent);

/**
 * @brief Flat tab strip of the Network workspace.
 *
 * The control draws one tab per added label, starting at the left edge of the
 * client area. The selected tab uses a white background with an accent bar
 * along its top edge, an unselected tab keeps the background of the strip and
 * a hovered tab is highlighted. The tabs are painted by the control itself,
 * so no child windows are involved and the strip keeps a fixed height.
 *
 * The width of a tab is derived from its label once and then reused by the
 * painting and by the hit test, so both agree on the position of a tab.
 */
class NetworkTabBar : public wxPanel
{
public:
    /**
     * @brief Create the tab strip.
     * @param[in] parent Parent window.
     * @param[in] id Window identifier.
     */
    NetworkTabBar(wxWindow* parent, wxWindowID id);

    /**
     * @brief Append one tab.
     * @param[in] label Label of the tab.
     */
    void AddTab(const wxString& label);

    /**
     * @brief Get the index of the selected tab.
     * @return The index of the selected tab, wxNOT_FOUND when the strip is
     *         empty.
     */
    int GetSelection() const;

    /**
     * @brief Select a tab without notifying the parent window.
     *
     * The call is used to show the initial state of the strip; a selection
     * made by the user goes through the notification of the control.
     *
     * @param[in] index Index of the tab to select.
     */
    void SetSelection(int index);

private:
    /**
     * @brief One tab of the strip.
     */
    struct Tab
    {
        wxString label;     ///< Label of the tab.
        int      width = 0; ///< Width of the tab.
    };

    /**
     * @brief Get the rectangle of every tab.
     * @return The rectangles in tab order, relative to the client area.
     */
    std::vector<wxRect> TabRects() const;

    /**
     * @brief Find the tab below a client position.
     * @param[in] position Client position.
     * @return The tab index, wxNOT_FOUND when no tab is hit.
     */
    int HitTest(const wxPoint& position) const;

    /**
     * @brief Draw the strip.
     * @param[in] event Paint event.
     */
    void OnPaint(wxPaintEvent& event);

    /**
     * @brief Activate the tab below the cursor.
     * @param[in] event Mouse event.
     */
    void OnLeftDown(wxMouseEvent& event);

    /**
     * @brief Track the hovered tab.
     * @param[in] event Mouse event.
     */
    void OnMotion(wxMouseEvent& event);

    /**
     * @brief Drop the hovered tab.
     * @param[in] event Mouse event.
     */
    void OnLeaveWindow(wxMouseEvent& event);

    /**
     * @brief Select a tab and notify the parent window.
     * @param[in] index Index of the tab.
     */
    void Select(int index);

    std::vector<Tab> tabs_;
    std::size_t      selection_ = 0;
    int              hovered_ = wxNOT_FOUND;
};

#endif // APPBOX_PACKER_WIDGET_NETWORK_TAB_BAR_HPP
