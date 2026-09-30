#include "Toolbar.hpp"
#include <wx/artprov.h>

extern const int kToolbarStartupFiles = wxNewId();
extern const int kToolbarBuild = wxNewId();
extern const int kToolbarBuildAndRun = wxNewId();

namespace
{

/*
 * Icon size of a tool. 16x16 is the tool bitmap size the native Windows
 * toolbar uses by default, so the bar keeps the size of a standard toolbar of
 * the system (the registry browser of the launcher draws its tools the same way).
 * The size has to be requested explicitly: without it the art provider returns
 * the size hint of the `wxART_TOOLBAR` client, which is 24x24 on Windows.
 */
const wxSize kIconSize(16, 16);

/**
 * @brief Load one art provider icon at the requested size.
 * @param[in] art Art provider identifier.
 * @param[in] size Requested icon size.
 * @return The icon bitmap.
 */
wxBitmap LoadIcon(const wxString& art, const wxSize& size)
{
    return wxArtProvider::GetBitmap(art, wxART_TOOLBAR, size);
}

} // namespace

Toolbar::Toolbar(wxWindow* parent, wxWindowID id)
    : wxToolBar(parent, id, wxDefaultPosition, wxDefaultSize,
                /*
                 * wxTB_TEXT draws the label of a tool; wxTB_HORZ_LAYOUT is left
                 * out on purpose, because it turns the label to the right of
                 * the icon while the bar shows it below the icon.
                 */
                wxTB_HORIZONTAL | wxTB_TEXT | wxTB_FLAT | wxTB_NODIVIDER)
{
    SetToolBitmapSize(kIconSize);

    AddTool(kToolbarStartupFiles, "Startup Files", LoadIcon(wxART_FILE_OPEN, kIconSize),
            "Browse the imported folders and manage the startup files");
    AddTool(kToolbarBuild, "Build", LoadIcon(wxART_FILE_SAVE, kIconSize),
            "Pack the imported folders and the launcher into a zip archive");
    /*
     * The run command carries the forward arrow of the art provider: it is the
     * icon of the standard set which reads as "run", and the executable file
     * icon the command used before described the artifact rather than the
     * action.
     */
    AddTool(kToolbarBuildAndRun, "Build and Run", LoadIcon(wxART_GO_FORWARD, kIconSize),
            "Pack the archive, extract it to a temporary folder and start the launcher");

    Realize();
}

void Toolbar::SetBuildAndRunEnabled(bool enabled)
{
    EnableTool(kToolbarBuildAndRun, enabled);
}
