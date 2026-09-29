#include "RibbonBar.hpp"
#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/combobox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <cstddef>

extern const int kRibbonStartupFiles = wxNewId();
extern const int kRibbonBuild = wxNewId();
extern const int kRibbonBuildAndRun = wxNewId();
extern const int kRibbonBrowseOutput = wxNewId();
extern const int kRibbonOutputPath = wxNewId();
extern const int kRibbonProjectType = wxNewId();

namespace
{

/** Icon size of a large ribbon button. */
const wxSize kLargeIconSize(32, 32);

/** Icon size of a small ribbon button. */
const wxSize kSmallIconSize(16, 16);

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

RibbonBar::RibbonBar(wxWindow* parent, wxWindowID id)
    : wxRibbonBar(parent, id, wxDefaultPosition, wxDefaultSize, wxRIBBON_BAR_DEFAULT_STYLE)
{
    CreateHomePage();
    CreateAdvancedPage();

    SetActivePage(static_cast<size_t>(0));
    Realize();
}

wxString RibbonBar::GetOutputPath() const
{
    return output_path_ != nullptr ? output_path_->GetValue() : wxString();
}

void RibbonBar::SetOutputPath(const wxString& path)
{
    if (output_path_ != nullptr)
    {
        output_path_->ChangeValue(path);
    }
}

appbox::ProjectType RibbonBar::GetProjectType() const
{
    if (project_type_ == nullptr)
    {
        return appbox::ProjectType::Standalone;
    }

    const auto selection = project_type_->GetSelection();
    if (selection < 0)
    {
        return appbox::ProjectType::Standalone;
    }

    return appbox::ProjectTypeAt(static_cast<std::size_t>(selection));
}

void RibbonBar::SetProjectType(appbox::ProjectType type)
{
    if (project_type_ == nullptr)
    {
        return;
    }

    /* SetSelection() does not raise a command event, so this is not a re-entry. */
    project_type_->SetSelection(static_cast<int>(appbox::ProjectTypeIndexOf(type)));
}

void RibbonBar::SetBuildAndRunEnabled(bool enabled)
{
    if (build_bar_ != nullptr)
    {
        build_bar_->EnableButton(kRibbonBuildAndRun, enabled);
    }
}

wxRibbonPage* RibbonBar::AppendRibbonPage(const wxString& label)
{
    return new wxRibbonPage(this, wxID_ANY, label);
}

wxRibbonButtonBar* RibbonBar::AppendButtonGroup(wxRibbonPage* page, const wxString& label)
{
    auto* panel = new wxRibbonPanel(page, wxID_ANY, label);
    return new wxRibbonButtonBar(panel);
}

void RibbonBar::AddLargeButton(wxRibbonButtonBar* bar, int id, const wxString& label, const wxString& art,
                               const wxString& help, bool enabled)
{
    bar->AddButton(id, label, LoadIcon(art, kLargeIconSize), help);
    bar->EnableButton(id, enabled);
}

void RibbonBar::AddSmallButton(wxRibbonButtonBar* bar, int id, const wxString& label, const wxString& art,
                               const wxString& help, bool enabled)
{
    bar->AddButton(id, label, LoadIcon(art, kLargeIconSize), LoadIcon(art, kSmallIconSize), wxNullBitmap, wxNullBitmap,
                   wxRIBBON_BUTTON_NORMAL, help);
    bar->EnableButton(id, enabled);
}

void RibbonBar::AppendOutputGroup(wxRibbonPage* page)
{
    auto* panel = new wxRibbonPanel(page, wxID_ANY, "Output");
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* file_row = new wxBoxSizer(wxHORIZONTAL);
    file_row->Add(new wxStaticText(panel, wxID_ANY, "Output File:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    output_path_ = new wxTextCtrl(panel, kRibbonOutputPath);
    output_path_->SetToolTip("Destination archive written by the Build command");
    file_row->Add(output_path_, 1, wxALIGN_CENTER_VERTICAL);
    file_row->Add(new wxButton(panel, kRibbonBrowseOutput, "Browse..."), 0, wxLEFT, 6);
    sizer->Add(file_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

    auto* type_row = new wxBoxSizer(wxHORIZONTAL);
    type_row->Add(new wxStaticText(panel, wxID_ANY, "Project Type:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    /*
     * The box lists the project types in the order of the core enumeration, so
     * the selection is the index of the type it shows.
     */
    wxArrayString project_types;
    for (std::size_t index = 0; index < appbox::ProjectTypeCount(); ++index)
    {
        project_types.Add(appbox::ProjectTypeDisplayName(appbox::ProjectTypeAt(index)));
    }

    project_type_ =
        new wxComboBox(panel, kRibbonProjectType, appbox::ProjectTypeDisplayName(appbox::ProjectType::Standalone),
                       wxDefaultPosition, wxDefaultSize, project_types, wxCB_READONLY);
    project_type_->SetSelection(static_cast<int>(appbox::ProjectTypeIndexOf(appbox::ProjectType::Standalone)));
    project_type_->SetToolTip("Standalone writes a self-contained archive with the loader; Patch writes the resources "
                              "of the app directory without a loader, for the patch directory next to it");
    type_row->Add(project_type_, 1, wxALIGN_CENTER_VERTICAL);

    auto* options = new wxButton(panel, wxID_ANY, "Options");
    options->Enable(false);
    type_row->Add(options, 0, wxLEFT, 6);
    sizer->Add(type_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 6);

    panel->SetSizer(sizer);
}

void RibbonBar::CreateHomePage()
{
    auto* page = AppendRibbonPage("Home");

    auto* capture = AppendButtonGroup(page, "Capture");
    AddLargeButton(capture, wxNewId(), "Start Capture", wxART_FIND, "Capture the changes of a running application",
                   false);
    AddSmallButton(capture, wxNewId(), "Capture Before", wxART_GO_BACK, "Take a snapshot before the capture starts",
                   false);
    AddSmallButton(capture, wxNewId(), "Capture and Diff", wxART_GO_FORWARD,
                   "Capture and compare against the previous snapshot", false);

    auto* snapshot = AppendButtonGroup(page, "Snapshot");
    AddLargeButton(snapshot, wxNewId(), "Snapshot", wxART_FOLDER, "Store the current filesystem snapshot", false);
    AddSmallButton(snapshot, wxNewId(), "Merge Snapshot", wxART_REDO, "Merge a stored snapshot into the project",
                   false);

    build_bar_ = AppendButtonGroup(page, "Build");
    AddLargeButton(build_bar_, kRibbonBuild, "Build", wxART_FILE_SAVE,
                   "Pack the imported folders and the loader into a zip archive", true);
    AddSmallButton(build_bar_, kRibbonBuildAndRun, "Build and Run", wxART_EXECUTABLE_FILE,
                   "Pack the archive, extract it to a temporary folder and start the loader", true);
    AddSmallButton(build_bar_, wxNewId(), "Run and Merge", wxART_REDO,
                   "Run the packaged application and merge the overlay back", false);

    auto* startup = AppendButtonGroup(page, "Startup");
    AddLargeButton(startup, kRibbonStartupFiles, "Startup Files", wxART_FILE_OPEN,
                   "Browse the imported folders and manage the startup files", true);

    AppendOutputGroup(page);

    auto* publish = AppendButtonGroup(page, "Publish");
    AddLargeButton(publish, wxNewId(), "Publish to Server", wxART_GO_UP, "Upload the archive to a remote repository",
                   false);
    AddLargeButton(publish, wxNewId(), "Publish to Local Repository", wxART_HARDDISK,
                   "Copy the archive into the local package repository", false);
}

void RibbonBar::CreateAdvancedPage()
{
    auto* page = AppendRibbonPage("Advanced");

    auto* diagnostics = AppendButtonGroup(page, "Diagnostics");
    AddLargeButton(diagnostics, wxNewId(), "Verify Archive", wxART_INFORMATION,
                   "Check the entries of a written archive", false);
    AddSmallButton(diagnostics, wxNewId(), "Show Log", wxART_LIST_VIEW, "Show the packer log of the last run", false);
    AddSmallButton(diagnostics, wxNewId(), "Clear Log", wxART_DELETE, "Drop the buffered log of the last run", false);

    auto* layers = AppendButtonGroup(page, "Layers");
    AddLargeButton(layers, wxNewId(), "Edit Layers", wxART_FOLDER, "Reorder the lower filesystem layers of the project",
                   false);
}
