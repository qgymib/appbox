#include "RegistryIsolationDialog.hpp"
#include <wx/checkbox.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace
{

/** Border of the dialog and gap between its parts. */
constexpr int kBorder = 12;

} // namespace

RegistryIsolationDialog::RegistryIsolationDialog(wxWindow* parent, const wxString& key_path,
                                                 appbox::RegistryKeyIsolation initial)
    : wxDialog(parent, wxID_ANY, "Isolation Mode", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    wxArrayString modes;
    for (const auto& name : appbox::RegistryKeyIsolationNames())
    {
        modes.Add(wxString(name));
    }

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(new wxStaticText(this, wxID_ANY, "Key: " + key_path), 0, wxALL, kBorder);

    /*
     * The names of the modes are ordered like the enumeration of the modes, so
     * the selection of the field is the mode itself.
     */
    isolation_ =
        new wxRadioBox(this, wxID_ANY, "Isolation", wxDefaultPosition, wxDefaultSize, modes, 1, wxRA_SPECIFY_COLS);
    isolation_->SetSelection(static_cast<int>(initial));
    sizer->Add(isolation_, 0, wxLEFT | wxRIGHT, kBorder);

    /*
     * The recursion is the only option of the dialog: without it the mode
     * reaches the key alone, with it the sub keys and the values below the key
     * follow the mode as well.
     */
    recursive_ = new wxCheckBox(this, wxID_ANY, "Apply to the whole subtree");
    sizer->Add(recursive_, 0, wxLEFT | wxRIGHT | wxTOP, kBorder);

    if (auto* buttons = CreateButtonSizer(wxOK | wxCANCEL))
    {
        sizer->Add(buttons, 0, wxEXPAND | wxALL, kBorder);
    }

    SetSizerAndFit(sizer);
    CentreOnParent();
}

appbox::RegistryKeyIsolation RegistryIsolationDialog::Isolation() const
{
    const int selection = isolation_ != nullptr ? isolation_->GetSelection() : wxNOT_FOUND;
    if (selection < 0)
    {
        return appbox::RegistryKeyIsolation::Merge;
    }
    return static_cast<appbox::RegistryKeyIsolation>(selection);
}

bool RegistryIsolationDialog::Recursive() const
{
    return recursive_ != nullptr && recursive_->GetValue();
}
