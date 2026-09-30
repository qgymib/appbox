#ifndef APPBOX_PACKER_WIDGET_TOOLBAR_HPP
#define APPBOX_PACKER_WIDGET_TOOLBAR_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include <wx/toolbar.h>

/** Command id of the "Startup Files" tool. */
extern const int kToolbarStartupFiles;

/** Command id of the "Build" tool. */
extern const int kToolbarBuild;

/** Command id of the "Build and Run" tool. */
extern const int kToolbarBuildAndRun;

/**
 * @brief Native toolbar of the packer.
 *
 * The bar carries the commands of the packaging workflow in the order they are
 * used: `Startup Files` selects the main program, `Build` writes the archive
 * and `Build and Run` writes the archive and starts it. Every tool is drawn
 * with its icon above its label and explains itself with a tooltip.
 *
 * The bar only raises tool events; every action lives in the frame.
 */
class Toolbar : public wxToolBar
{
public:
    /**
     * @brief Create the toolbar with the commands of the workflow.
     * @param[in] parent Parent window.
     * @param[in] id Window identifier.
     */
    Toolbar(wxWindow* parent, wxWindowID id);

    /**
     * @brief Enable or disable the "Build and Run" tool.
     *
     * A patch package carries no loader, so the run command is only offered for
     * a standalone project.
     *
     * @param[in] enabled Whether the tool accepts input.
     */
    void SetBuildAndRunEnabled(bool enabled);
};

#endif // APPBOX_PACKER_WIDGET_TOOLBAR_HPP
