#ifndef APPBOX_PACKER_WIDGET_DEBUG_PANEL_HPP
#define APPBOX_PACKER_WIDGET_DEBUG_PANEL_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>

class TabBar;
class wxSimplebook;

/**
 * @brief Debug workspace of the packer.
 *
 * The workspace holds a tab strip above the page of the selected tab, the way
 * the Settings workspace does. The `Trace` tab carries the tracer: it runs a
 * program under `cdb.exe` and shows which of the functions of a view the
 * program used. The strip is shared with the other workspaces, so a further
 * tab is added by appending to the strip and to the book.
 *
 * The workspace holds no model of its own: the page owns its state, and the
 * frame neither pushes a value into the workspace nor reads one back from it.
 */
class DebugPanel : public wxPanel
{
public:
    /**
     * @brief Create the debug workspace.
     * @param[in] parent Parent window.
     */
    explicit DebugPanel(wxWindow* parent);

private:
    /**
     * @brief Create the page of the `Trace` tab.
     * @param[in] parent Parent window of the page.
     * @return The created page.
     */
    wxWindow* CreateTracePage(wxWindow* parent);

    /**
     * @brief Show the page of the activated tab.
     * @param[in] event Command event carrying the tab index.
     */
    void OnTabChanged(wxCommandEvent& event);

    TabBar*       tab_bar_ = nullptr;
    wxSimplebook* pages_ = nullptr;
};

#endif // APPBOX_PACKER_WIDGET_DEBUG_PANEL_HPP
