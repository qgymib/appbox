#include "RegistryKeyDialog.hpp"
#include "core/RegistryModel.hpp"
#include <wx/button.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

RegistryKeyDialog::RegistryKeyDialog(wxWindow* parent, const wxString& title, const wxString& parent_path,
                                     const wxString& initial_name, Validator validator)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize),
      validator_(std::move(validator))
{
    const auto shown_path = parent_path.empty() ? wxString(appbox::kRegistryContainerLabel) : parent_path;

    auto* path_label = new wxStaticText(this, wxID_ANY, "Parent key:");
    auto* path_value = new wxStaticText(this, wxID_ANY, shown_path);
    path_value->SetFont(path_value->GetFont().Bold());

    auto* name_label = new wxStaticText(this, wxID_ANY, "Name:");
    name_ = new wxTextCtrl(this, wxID_ANY, initial_name, wxDefaultPosition, wxSize(320, -1));
    name_->SetToolTip("Name of the key below its parent key");

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
    sizer->Add(grid, 1, wxEXPAND | wxALL, 12);
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
