#include "RegistryKeyDialog.hpp"
#include "core/RegistryModel.hpp"
#include <wx/button.h>
#include <wx/msgdlg.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

RegistryKeyDialog::RegistryKeyDialog(wxWindow* parent, const wxString& title, const wxString& parent_path,
                                     const wxString& initial_name, appbox::RegistryKeyIsolation initial_isolation,
                                     Validator validator)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize), validator_(std::move(validator))
{
    const auto shown_path = parent_path.empty() ? wxString(appbox::kRegistryContainerLabel) : parent_path;

    auto* path_label = new wxStaticText(this, wxID_ANY, "Parent key:");
    auto* path_value = new wxStaticText(this, wxID_ANY, shown_path);
    path_value->SetFont(path_value->GetFont().Bold());

    auto* name_label = new wxStaticText(this, wxID_ANY, "Name:");
    name_ = new wxTextCtrl(this, wxID_ANY, initial_name, wxDefaultPosition, wxSize(320, -1));
    name_->SetToolTip("Name of the key below its parent key");

    /*
     * The names of the modes are ordered like the enumeration of the modes, so
     * the selection of the field is the mode itself.
     */
    wxArrayString modes;
    for (const auto& mode : appbox::RegistryKeyIsolationNames())
    {
        modes.Add(wxString(mode));
    }

    isolation_ =
        new wxRadioBox(this, wxID_ANY, "Isolation", wxDefaultPosition, wxDefaultSize, modes, 1, wxRA_SPECIFY_COLS);
    isolation_->SetSelection(static_cast<int>(initial_isolation));
    isolation_->SetToolTip("Isolation mode of the key, which decides what the application sees and where its "
                           "modifications land");

    auto* grid = new wxFlexGridSizer(2, 8, 8);
    grid->Add(path_label, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(path_value, 1, wxALIGN_CENTER_VERTICAL);
    grid->Add(name_label, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(name_, 1, wxEXPAND);

    auto* buttons = new wxStdDialogButtonSizer();
    buttons->AddButton(new wxButton(this, wxID_OK));
    buttons->AddButton(new wxButton(this, wxID_CANCEL));
    buttons->Realize();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(grid, 0, wxEXPAND | wxALL, 12);
    sizer->Add(isolation_, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    SetSizerAndFit(sizer);
    SetMinSize(wxSize(420, GetSize().GetHeight()));

    name_->SetFocus();
    name_->SelectAll();

    Bind(wxEVT_BUTTON, &RegistryKeyDialog::OnOk, this, wxID_OK);
}

std::wstring RegistryKeyDialog::Name() const
{
    return name_->GetValue().ToStdWstring();
}

appbox::RegistryKeyIsolation RegistryKeyDialog::Isolation() const
{
    const int selection = isolation_ != nullptr ? isolation_->GetSelection() : wxNOT_FOUND;
    if (selection < 0)
    {
        return appbox::RegistryKeyIsolation::Merge;
    }
    return static_cast<appbox::RegistryKeyIsolation>(selection);
}

void RegistryKeyDialog::OnOk(wxCommandEvent&)
{
    const auto name = Name();
    if (validator_ != nullptr)
    {
        const auto error = validator_(name);
        if (!error.empty())
        {
            wxMessageBox(wxString::FromUTF8(error), GetTitle(), wxOK | wxICON_WARNING, this);
            return;
        }
    }

    EndModal(wxID_OK);
}
