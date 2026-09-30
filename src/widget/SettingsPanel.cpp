#include "SettingsPanel.hpp"
#include "TabBar.hpp"
#include <wx/button.h>
#include <wx/dcclient.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <cstddef>

extern const int kSettingsOutputPath = wxNewId();
extern const int kSettingsBrowseOutput = wxNewId();
extern const int kSettingsProjectType = wxNewId();

namespace
{

/**
 * @brief Pages of the Settings workspace.
 *
 * The values are the positions of the pages inside the tab strip as well.
 */
enum class SettingsPage
{
    Output = 0 ///< Destination archive and project type of the session.
};

/** Page the workspace opens on. */
constexpr SettingsPage kStartPage = SettingsPage::Output;

/** Border of the form of the Output tab. */
constexpr int kFormBorder = 12;

/** Gap between two rows of the form. */
constexpr int kRowGap = 12;

/** Gap between two columns of the form, which is the gap between a label and its control. */
constexpr int kFieldGap = 8;

/** Minimum width of the project type box of the form. */
constexpr int kFieldWidth = 260;

/** Rows of the form of the Output tab, one per option. */
constexpr int kFormRows = 2;

/** Columns of the form: the labels, the controls and the button of the path row. */
constexpr int kFormColumns = 3;

/** Column of the form which holds the controls; the column takes the spare width. */
constexpr int kControlColumn = 1;

/**
 * Slack which is left between the end of the archive path and the edge of its
 * box before the path counts as cut off, so that a path which ends right at the
 * edge of the box is still reported as cut off.
 */
constexpr int kTruncationSlack = 2;

/** Label of the archive path row. */
const char* const kOutputFileLabel = "Output File:";

/** Label of the project type row. */
const char* const kProjectTypeLabel = "Project Type:";

/** Tooltip of the `Browse...` button. */
const char* const kBrowseTooltip = "Choose the destination archive of the Build command";

/** Tooltip of the project type box. */
const char* const kProjectTypeTooltip =
    "Standalone writes a self-contained archive with the launcher; Patch writes the resources "
    "of the app directory without a launcher, for the patch directory next to it";

} // namespace

SettingsPanel::SettingsPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
    tab_bar_ = new TabBar(this, wxID_ANY);
    tab_bar_->AddTab("Output");
    tab_bar_->SetSelection(static_cast<int>(kStartPage));

    /*
     * The book holds the page of every tab of the strip. The strip offers the
     * Output tab only for now, so the two are kept in step by adding a tab to
     * both of them.
     */
    pages_ = new wxSimplebook(this, wxID_ANY);
    pages_->AddPage(CreateOutputPage(pages_), "Output");
    pages_->SetSelection(static_cast<std::size_t>(kStartPage));

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(tab_bar_, 0, wxEXPAND);
    sizer->Add(pages_, 1, wxEXPAND);
    SetSizer(sizer);

    Bind(APPBOX_TAB, &SettingsPanel::OnTabChanged, this);
}

wxString SettingsPanel::GetOutputPath() const
{
    return output_path_ != nullptr ? output_path_->GetValue() : wxString();
}

void SettingsPanel::SetOutputPath(const wxString& path)
{
    if (output_path_ != nullptr)
    {
        output_path_->ChangeValue(path);
    }
}

appbox::ProjectType SettingsPanel::GetProjectType() const
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

void SettingsPanel::SetProjectType(appbox::ProjectType type)
{
    if (project_type_ == nullptr)
    {
        return;
    }

    /* SetSelection() does not raise a command event, so this is not a re-entry. */
    project_type_->SetSelection(static_cast<int>(appbox::ProjectTypeIndexOf(type)));
}

wxWindow* SettingsPanel::CreateOutputPage(wxWindow* parent)
{
    auto* page = new wxPanel(parent, wxID_ANY);

    output_path_ = new wxTextCtrl(page, kSettingsOutputPath);
    output_path_->Bind(wxEVT_MOTION, &SettingsPanel::OnOutputPathMotion, this);
    output_path_->Bind(wxEVT_LEAVE_WINDOW, &SettingsPanel::OnOutputPathLeave, this);

    auto* browse = new wxButton(page, kSettingsBrowseOutput, "Browse...");
    browse->SetToolTip(kBrowseTooltip);

    /*
     * A box of a grid cell fills its whole cell, and the height of a cell is
     * the height of the tallest item of its row, so a box which is added to the
     * grid on its own is stretched to the height of the row. The two boxes are
     * therefore added through a row of their own, which takes the width of the
     * cell while the box keeps its own height.
     */
    auto* path_cell = new wxBoxSizer(wxHORIZONTAL);
    path_cell->Add(output_path_, 1, wxALIGN_CENTER_VERTICAL);

    /*
     * The box lists the project types in the order of the core enumeration, so
     * the selection is the index of the type it shows. The width follows the
     * column of the grid, which lines the box up with the archive path box; the
     * minimum width keeps it readable while the window is narrow.
     */
    wxArrayString project_types;
    for (std::size_t index = 0; index < appbox::ProjectTypeCount(); ++index)
    {
        project_types.Add(appbox::ProjectTypeDisplayName(appbox::ProjectTypeAt(index)));
    }

    project_type_ =
        new wxComboBox(page, kSettingsProjectType, appbox::ProjectTypeDisplayName(appbox::ProjectType::Standalone),
                       wxDefaultPosition, wxDefaultSize, project_types, wxCB_READONLY);
    project_type_->SetMinSize(wxSize(kFieldWidth, -1));
    project_type_->SetSelection(static_cast<int>(appbox::ProjectTypeIndexOf(appbox::ProjectType::Standalone)));
    project_type_->SetToolTip(kProjectTypeTooltip);

    auto* type_cell = new wxBoxSizer(wxHORIZONTAL);
    type_cell->Add(project_type_, 1, wxALIGN_CENTER_VERTICAL);

    /*
     * The two options share one grid, which lines the labels up in the first
     * column and the controls up in the second one: the archive path box and
     * the project type box share their left and their right edge, because the
     * second column is the only one which takes the spare width of the form.
     */
    auto* grid = new wxFlexGridSizer(kFormRows, kFormColumns, kRowGap, kFieldGap);
    grid->AddGrowableCol(kControlColumn, 1);
    grid->Add(new wxStaticText(page, wxID_ANY, kOutputFileLabel), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(path_cell, 1, wxEXPAND);
    grid->Add(browse, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(page, wxID_ANY, kProjectTypeLabel), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(type_cell, 1, wxEXPAND);

    /* The third column of the second row holds no control; the cell keeps the columns of the two rows aligned. */
    grid->Add(0, 0);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kFormBorder);
    page->SetSizer(sizer);
    return page;
}

void SettingsPanel::OnTabChanged(wxCommandEvent& event)
{
    const auto index = event.GetInt();
    if (index >= 0 && static_cast<std::size_t>(index) < pages_->GetPageCount())
    {
        pages_->SetSelection(static_cast<std::size_t>(index));
    }
    event.Skip();
}

void SettingsPanel::OnOutputPathMotion(wxMouseEvent& event)
{
    UpdateOutputPathToolTip();
    event.Skip();
}

void SettingsPanel::OnOutputPathLeave(wxMouseEvent& event)
{
    SetOutputPathToolTip(wxString());
    event.Skip();
}

void SettingsPanel::UpdateOutputPathToolTip()
{
    if (output_path_ == nullptr)
    {
        return;
    }

    /*
     * The whole path is offered while the box cuts it off, and no tooltip is
     * offered while the box shows the whole path: the tooltip is the only way
     * to read a path the box cannot show in full.
     */
    const wxString path = output_path_->GetValue();
    SetOutputPathToolTip(IsOutputPathTruncated(path) ? path : wxString());
}

bool SettingsPanel::IsOutputPathTruncated(const wxString& path)
{
    if (output_path_ == nullptr || path.empty())
    {
        return false;
    }

    /*
     * A box which was not laid out yet has no width to show a path in, and
     * measuring it would report every path as cut off.
     */
    const int     width = output_path_->GetClientSize().GetWidth();
    const wxPoint margins = output_path_->GetMargins();
    const int     available = width - 2 * margins.x - kTruncationSlack;
    if (available <= 0)
    {
        return false;
    }

    /*
     * The text is measured with the font of the box instead of asking the
     * control where its last character sits: the position of a character
     * depends on the scroll offset of the box, while the question is whether
     * the whole path fits into the width of the box at all.
     */
    wxClientDC dc(output_path_);
    dc.SetFont(output_path_->GetFont());
    return dc.GetTextExtent(path).GetWidth() > available;
}

void SettingsPanel::SetOutputPathToolTip(const wxString& text)
{
    if (output_path_ == nullptr || text == output_path_tooltip_)
    {
        return;
    }

    /*
     * wxWidgets pops the tooltip of a window up after the cursor rested on it,
     * so a text which is already shown is left in place: writing it again would
     * restart the delay.
     */
    output_path_tooltip_ = text;
    if (text.empty())
    {
        output_path_->UnsetToolTip();
    }
    else
    {
        output_path_->SetToolTip(text);
    }
}
