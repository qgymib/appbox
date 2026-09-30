#include "FilesystemIsolationDialog.hpp"
#include <wx/checkbox.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace
{

/** Border of the dialog and gap between its parts. */
constexpr int kBorder = 12;

} // namespace

FilesystemIsolationDialog::FilesystemIsolationDialog(wxWindow* parent, const wxString& target,
                                                     appbox::FilesystemIsolation initial)
    : wxDialog(parent, wxID_ANY, "Isolation Mode", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    wxArrayString modes;
    for (const auto& name : appbox::FilesystemIsolationNamesFor(appbox::FilesystemEntryKind::Directory))
    {
        modes.Add(wxString(name));
    }

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(new wxStaticText(this, wxID_ANY, target), 0, wxALL, kBorder);

    /*
     * The names of the modes are ordered like the enumeration of the modes, so
     * the selection of the field is the mode itself.
     */
    isolation_ =
        new wxRadioBox(this, wxID_ANY, "Isolation", wxDefaultPosition, wxDefaultSize, modes, 1, wxRA_SPECIFY_COLS);
    isolation_->SetSelection(static_cast<int>(initial));
    sizer->Add(isolation_, 0, wxLEFT | wxRIGHT, kBorder);

    subfolders_ = new wxCheckBox(this, wxID_ANY, "Apply to all subfolders");
    sizer->Add(subfolders_, 0, wxLEFT | wxRIGHT | wxTOP, kBorder);

    if (auto* buttons = CreateButtonSizer(wxOK | wxCANCEL))
    {
        sizer->Add(buttons, 0, wxEXPAND | wxALL, kBorder);
    }

    SetSizerAndFit(sizer);
    CentreOnParent();
}

appbox::FilesystemIsolation FilesystemIsolationDialog::Isolation() const
{
    const int selection = isolation_ != nullptr ? isolation_->GetSelection() : wxNOT_FOUND;
    if (selection < 0)
    {
        return appbox::FilesystemIsolation::WriteCopy;
    }
    return static_cast<appbox::FilesystemIsolation>(selection);
}

bool FilesystemIsolationDialog::ApplyToSubfolders() const
{
    return subfolders_ != nullptr && subfolders_->GetValue();
}
