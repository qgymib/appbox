#include "AboutDialog.hpp"
#include <wx/dataview.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/toplevel.h>

namespace
{

/** Border of the dialog and gap between its parts. */
constexpr int kBorder = 12;

/** Name shown as the title of the dialog and above the summary. */
constexpr const char* kApplicationName = "AppBox";

/**
 * @brief Width the one sentence summary is wrapped at.
 *
 * The sentence is a fixed text, so a fixed width keeps the dialog compact and
 * keeps the layout independent of the length of the font of the system.
 */
constexpr int kSummaryWrapWidth = 380;

/** Row and column gap of the grid which holds the build details. */
constexpr int kDetailRowGap = 6;
constexpr int kDetailColumnGap = 18;

/** Size of the table which lists the third-party libraries. */
constexpr int kDependencyListWidth = 420;
constexpr int kDependencyListHeight = 180;

/** Width of the two columns of the dependency table. */
constexpr int kDependencyNameColumnWidth = 240;
constexpr int kDependencyVersionColumnWidth = 160;

/**
 * @brief Format the git revision together with the branch it belongs to.
 *
 * The revision alone does not tell whether the binary was built from a clean
 * commit, so the branch and the marker of a modified working tree follow in
 * parentheses. A build without git reports `unknown` and gets no parentheses.
 *
 * @param[in] info Build information of the binary.
 * @return Text of the git revision row.
 */
wxString GitDescription(const appbox::AboutInfo& info)
{
    if (info.git_revision == "unknown")
    {
        return "unknown";
    }

    wxString description = info.git_revision + " (" + info.git_branch;
    if (info.git_dirty)
    {
        description += ", modified";
    }

    return description + ")";
}

} // namespace

AboutDialog::AboutDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, wxString("About ") + kApplicationName, wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    const appbox::AboutInfo& info = appbox::GetAboutInfo();

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    CreateHeader(*sizer);
    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kBorder);
    CreateBuildDetails(*sizer, info);
    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kBorder);
    CreateDependencyList(*sizer, info);

    if (auto* buttons = CreateButtonSizer(wxOK))
    {
        sizer->Add(buttons, 0, wxEXPAND | wxALL, kBorder);
    }

    SetSizerAndFit(sizer);
    CentreOnParent();
}

void AboutDialog::CreateHeader(wxSizer& sizer)
{
    auto* header = new wxBoxSizer(wxHORIZONTAL);

    /*
     * The icon is taken from the parent window instead of loading the resource
     * again: the main window already installed the icon embedded by
     * resource.rc, so the dialog shows the same image without knowing the name
     * of the resource.
     */
    if (auto* top_level = wxDynamicCast(GetParent(), wxTopLevelWindow))
    {
        const wxIcon icon = top_level->GetIcon();
        if (icon.IsOk())
        {
            header->Add(new wxStaticBitmap(this, wxID_ANY, icon), 0, wxALIGN_TOP | wxRIGHT, kBorder);
        }
    }

    auto* texts = new wxBoxSizer(wxVERTICAL);

    auto* name = new wxStaticText(this, wxID_ANY, kApplicationName);
    wxFont name_font = name->GetFont();
    name_font.MakeBold().MakeLarger();
    name->SetFont(name_font);
    texts->Add(name, 0, wxBOTTOM, kDetailRowGap);

    auto* summary = new wxStaticText(this, wxID_ANY, appbox::kAboutSummary);
    summary->Wrap(kSummaryWrapWidth);
    texts->Add(summary, 0);

    header->Add(texts, 1, wxALIGN_TOP);
    sizer.Add(header, 0, wxEXPAND | wxALL, kBorder);
}

void AboutDialog::CreateBuildDetails(wxSizer& sizer, const appbox::AboutInfo& info)
{
    auto* grid = new wxFlexGridSizer(0, 2, kDetailRowGap, kDetailColumnGap);

    AddDetailRow(*grid, "Version", wxString(info.version));
    AddDetailRow(*grid, "Build date", wxString(info.build_date));
    AddDetailRow(*grid, "Git revision", GitDescription(info));

    sizer.Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kBorder);
}

void AboutDialog::CreateDependencyList(wxSizer& sizer, const appbox::AboutInfo& info)
{
    sizer.Add(new wxStaticText(this, wxID_ANY, "Third-party libraries"), 0,
              wxLEFT | wxRIGHT | wxTOP, kBorder);

    auto* list = new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition,
                                        wxSize(kDependencyListWidth, kDependencyListHeight),
                                        wxDV_ROW_LINES | wxDV_SINGLE);
    list->SetMinSize(wxSize(kDependencyListWidth, kDependencyListHeight));

    /*
     * Both columns are inert: the table reports the libraries the binary was
     * linked against and cannot be edited. The list is filled once here and is
     * never rebuilt from one of its own events.
     */
    list->AppendTextColumn("Library", wxDATAVIEW_CELL_INERT, kDependencyNameColumnWidth);
    list->AppendTextColumn("Version", wxDATAVIEW_CELL_INERT, kDependencyVersionColumnWidth);

    for (const auto& dependency : info.dependencies)
    {
        wxVector<wxVariant> row;
        row.push_back(wxVariant(wxString(dependency.name)));
        row.push_back(wxVariant(wxString(dependency.version)));
        list->AppendItem(row);
    }

    sizer.Add(list, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kBorder);
}

void AboutDialog::AddDetailRow(wxFlexGridSizer& grid, const wxString& label, const wxString& value)
{
    grid.Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    grid.Add(new wxStaticText(this, wxID_ANY, value), 0, wxALIGN_CENTER_VERTICAL);
}
