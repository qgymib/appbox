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
                                                 appbox::RegistryIsolation initial)
    : wxDialog(parent, wxID_ANY, "Isolation Mode", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    wxArrayString modes;
    for (const auto& name : appbox::RegistryIsolationNames())
    {
        modes.Add(wxString(name));
    }

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(new wxStaticText(this, wxID_ANY, "Key: " + key_path), 0, wxALL, kBorder);

    /*
     * The names of the modes are ordered like the enumeration of the modes, so
     * the selection of the field is the mode itself.
     */
    isolation_ = new wxRadioBox(this, wxID_ANY, "Isolation", wxDefaultPosition, wxDefaultSize, modes, 1,
                                wxRA_SPECIFY_COLS);
    isolation_->SetSelection(static_cast<int>(initial));
    sizer->Add(isolation_, 0, wxLEFT | wxRIGHT, kBorder);

    sub_keys_ = new wxCheckBox(this, wxID_ANY, "Apply to all sub keys");
    sub_keys_->Bind(wxEVT_CHECKBOX, &RegistryIsolationDialog::OnSubKeysToggled, this);
    sizer->Add(sub_keys_, 0, wxLEFT | wxRIGHT | wxTOP, kBorder);

    values_ = new wxCheckBox(this, wxID_ANY, "Apply to all values in the subtree");
    values_->SetValue(true);
    values_->Enable(false);
    sizer->Add(values_, 0, wxLEFT | wxRIGHT | wxTOP, kBorder);

    if (auto* buttons = CreateButtonSizer(wxOK | wxCANCEL))
    {
        sizer->Add(buttons, 0, wxEXPAND | wxALL, kBorder);
    }

    SetSizerAndFit(sizer);
    CentreOnParent();
}

appbox::RegistryIsolation RegistryIsolationDialog::Isolation() const
{
    const int selection = isolation_ != nullptr ? isolation_->GetSelection() : wxNOT_FOUND;
    if (selection < 0)
    {
        return appbox::RegistryIsolation::WriteCopy;
    }
    return static_cast<appbox::RegistryIsolation>(selection);
}

bool RegistryIsolationDialog::ApplyToSubKeys() const
{
    return sub_keys_ != nullptr && sub_keys_->GetValue();
}

bool RegistryIsolationDialog::ApplyToValues() const
{
    return ApplyToSubKeys() && values_ != nullptr && values_->GetValue();
}

void RegistryIsolationDialog::OnSubKeysToggled(wxCommandEvent& event)
{
    /*
     * The values below the key are only overwritten together with the sub
     * keys: a mode which is applied to a key alone never reaches its values.
     */
    if (values_ != nullptr && sub_keys_ != nullptr)
    {
        values_->Enable(sub_keys_->GetValue());
    }
    event.Skip();
}
