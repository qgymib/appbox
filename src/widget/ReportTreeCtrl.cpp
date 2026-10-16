#include "ReportTreeCtrl.hpp"
#include <initializer_list>

ReportTreeCtrl::ReportTreeCtrl(wxWindow* parent, const wxSize& size)
    : wxTreeCtrl(parent, wxID_ANY, wxDefaultPosition, size, wxTR_NO_LINES | wxTR_SINGLE)
{
    /*
     * The keyboard cannot reach a report, and a tree which takes no input must
     * not be part of the order the `Tab` key walks.
     */
    DisableFocusFromKeyboard();

    /*
     * Every mouse event is answered by this window and not passed on, which
     * keeps the control of Windows from working it. The event of the wheel is
     * answered as well, because a report which the user may not pick must not
     * be scrolled away from under the pointer either.
     */
    for (const auto& type :
         { wxEVT_LEFT_DOWN, wxEVT_LEFT_UP, wxEVT_LEFT_DCLICK, wxEVT_RIGHT_DOWN, wxEVT_RIGHT_UP, wxEVT_RIGHT_DCLICK,
           wxEVT_MIDDLE_DOWN, wxEVT_MIDDLE_UP, wxEVT_MIDDLE_DCLICK, wxEVT_AUX1_DOWN, wxEVT_AUX1_UP, wxEVT_AUX1_DCLICK,
           wxEVT_AUX2_DOWN, wxEVT_AUX2_UP, wxEVT_AUX2_DCLICK, wxEVT_MOTION, wxEVT_MOUSEWHEEL })
    {
        Bind(type, &ReportTreeCtrl::OnMouse, this);
    }
}

void ReportTreeCtrl::OnMouse(wxMouseEvent& event)
{
    /*
     * The event is answered and not passed on: `event.Skip()` would let the
     * control of Windows work the click, which would pick an item, fold a
     * branch away and take the focus of the dialog.
     */
    wxUnusedVar(event);
}
