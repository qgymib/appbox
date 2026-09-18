#ifndef APPBOX_PACKER_WIDGET_PLACEHOLDER_PANEL_HPP
#define APPBOX_PACKER_WIDGET_PLACEHOLDER_PANEL_HPP

#include <wx/wx.h>

/**
 * @brief Empty state page for isolation domains which are not implemented yet.
 *
 * The page follows the workspace style of the packer: a header with the title
 * and a one line summary, a separator and a framed empty state area. The whole
 * page is painted by the control, so it scales without extra layout work.
 */
class PlaceholderPanel : public wxPanel
{
public:
    /**
     * @brief Create the placeholder page.
     * @param[in] parent Parent window.
     * @param[in] module_name Name of the reserved module, e.g. "Registry".
     * @param[in] description One line summary shown below the title.
     */
    PlaceholderPanel(wxWindow* parent, const wxString& module_name, const wxString& description);

private:
    /**
     * @brief Draw the header and the empty state area.
     * @param[in] event Paint event.
     */
    void OnPaint(wxPaintEvent& event);

    wxString module_name_;
    wxString description_;
};

#endif // APPBOX_PACKER_WIDGET_PLACEHOLDER_PANEL_HPP
