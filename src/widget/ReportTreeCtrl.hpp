#ifndef APPBOX_PACKER_WIDGET_REPORT_TREE_CTRL_HPP
#define APPBOX_PACKER_WIDGET_REPORT_TREE_CTRL_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include <wx/treectrl.h>

/**
 * @brief Tree which reports the items it holds and which the user cannot work.
 *
 * The tree draws no line which connects an item with its parent and no button
 * which folds a branch away, so it reads like the list of the items it reports.
 * It takes no input at all: every mouse event is answered by the tree itself
 * and is not passed on, so the control of Windows never sees the click and can
 * neither pick an item, nor fold a branch away, nor take the focus, and the
 * keyboard cannot reach the tree either.
 *
 * A caller marks the item it reports by the attributes of that item, see
 * `wxTreeCtrl::SetItemFont()` and `wxTreeCtrl::SetItemTextColour()`, because
 * the tree holds no selection: the mark is what the caller wrote and not what
 * the user picked.
 *
 * The way the click is refused follows the way wxWidgets delivers a mouse
 * message: the window procedure of a window answers the mouse event first and
 * passes the message to the control of Windows only when the event was left
 * unprocessed, so an event which is answered and not passed on never reaches
 * the control.
 */
class ReportTreeCtrl : public wxTreeCtrl
{
public:
    /**
     * @brief Create the tree.
     * @param[in] parent Parent window.
     * @param[in] size Size the tree reserves for itself.
     */
    ReportTreeCtrl(wxWindow* parent, const wxSize& size);

private:
    /**
     * @brief Answer a mouse event of the tree without passing it on.
     * @param[in] event Mouse event of the tree.
     */
    void OnMouse(wxMouseEvent& event);
};

#endif // APPBOX_PACKER_WIDGET_REPORT_TREE_CTRL_HPP
