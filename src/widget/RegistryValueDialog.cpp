#include "RegistryValueDialog.hpp"
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace
{

/**
 * @brief Whether a type is edited by the multi line field.
 * @param[in] type Type of the value.
 * @return true when the type uses the multi line editor.
 */
bool UsesMultiLineEditor(appbox::RegistryValueType type)
{
    return type == appbox::RegistryValueType::MultiString || type == appbox::RegistryValueType::Binary
           || type == appbox::RegistryValueType::None;
}

/**
 * @brief Get the hint describing the input a type expects.
 * @param[in] type Type of the value.
 * @return The hint text.
 */
wxString HintFor(appbox::RegistryValueType type)
{
    switch (type)
    {
    case appbox::RegistryValueType::String:
        return "Text of the value.";
    case appbox::RegistryValueType::ExpandString:
        return "Text of the value; environment variables such as %PATH% are expanded by the reader.";
    case appbox::RegistryValueType::Dword:
        return "32 bit number, decimal or hexadecimal with a 0x prefix.";
    case appbox::RegistryValueType::Qword:
        return "64 bit number, decimal or hexadecimal with a 0x prefix.";
    case appbox::RegistryValueType::MultiString:
        return "One string per line.";
    case appbox::RegistryValueType::Binary:
        return "Hexadecimal bytes, for example 01 02 FF.";
    case appbox::RegistryValueType::None:
        return "Untyped value: the data is preserved and cannot be edited.";
    }
    return {};
}

} // namespace

RegistryValueDialog::RegistryValueDialog(wxWindow* parent, const wxString& title,
                                         const wxString& parent_path, const wxString& initial_name,
                                         appbox::RegistryValueType initial_type,
                                         const std::vector<std::uint8_t>& initial_data,
                                         Validator validator)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize),
      validator_(std::move(validator)),
      data_(initial_data)
{
    const auto shown_path = parent_path.empty() ? wxString(appbox::kRegistryContainerLabel) : parent_path;

    auto* path_label = new wxStaticText(this, wxID_ANY, "Key:");
    auto* path_value = new wxStaticText(this, wxID_ANY, shown_path);
    path_value->SetFont(path_value->GetFont().Bold());

    auto* name_label = new wxStaticText(this, wxID_ANY, "Name:");
    name_ = new wxTextCtrl(this, wxID_ANY, initial_name, wxDefaultPosition, wxSize(320, -1));
    name_->SetToolTip("Name of the value; leave it empty for the default value of the key");

    auto* type_label = new wxStaticText(this, wxID_ANY, "Type:");
    type_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(160, -1));
    for (const auto type : appbox::RegistryValueTypes())
    {
        type_->Append(wxString(appbox::RegistryValueTypeName(type)));
    }

    const auto& types = appbox::RegistryValueTypes();
    for (std::size_t index = 0; index < types.size(); ++index)
    {
        if (types[index] == initial_type)
        {
            type_->SetSelection(static_cast<int>(index));
            break;
        }
    }
    if (type_->GetSelection() == wxNOT_FOUND)
    {
        type_->SetSelection(0);
    }

    auto* value_label = new wxStaticText(this, wxID_ANY, "Value:");
    single_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(320, -1));
    multi_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(320, 110),
                            wxTE_MULTILINE);

    auto* editors = new wxBoxSizer(wxVERTICAL);
    editors->Add(single_, 0, wxEXPAND);
    editors->Add(multi_, 1, wxEXPAND);

    hint_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    hint_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

    auto* grid = new wxFlexGridSizer(2, 8, 8);
    grid->Add(path_label, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(path_value, 1, wxALIGN_CENTER_VERTICAL);
    grid->Add(name_label, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(name_, 1, wxEXPAND);
    grid->Add(type_label, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(type_, 0);
    grid->Add(value_label, 0, wxALIGN_TOP);
    grid->Add(editors, 1, wxEXPAND);

    auto* buttons = new wxStdDialogButtonSizer();
    buttons->AddButton(new wxButton(this, wxID_OK));
    buttons->AddButton(new wxButton(this, wxID_CANCEL));
    buttons->Realize();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(grid, 1, wxEXPAND | wxALL, 12);
    sizer->Add(hint_, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    sizer->Add(buttons, 0, wxEXPAND | wxALL, 12);

    SetSizerAndFit(sizer);
    SetMinSize(wxSize(460, GetSize().GetHeight()));

    /* The editors are created for every type; only the matching one is shown. */
    ShowEditorFor(initial_type, initial_data);

    name_->SetFocus();

    type_->Bind(wxEVT_CHOICE, &RegistryValueDialog::OnTypeChanged, this);
    Bind(wxEVT_BUTTON, &RegistryValueDialog::OnOk, this, wxID_OK);
}

std::wstring RegistryValueDialog::Name() const
{
    return name_->GetValue().ToStdWstring();
}

appbox::RegistryValueType RegistryValueDialog::Type() const
{
    const auto& types = appbox::RegistryValueTypes();
    const int selection = type_->GetSelection();
    if (selection < 0 || static_cast<std::size_t>(selection) >= types.size())
    {
        return types.front();
    }
    return types[static_cast<std::size_t>(selection)];
}

const std::vector<std::uint8_t>& RegistryValueDialog::Data() const
{
    return data_;
}

void RegistryValueDialog::ShowEditorFor(appbox::RegistryValueType type,
                                        const std::vector<std::uint8_t>& data)
{
    const auto text = wxString(appbox::FormatRegistryValueText(type, data));
    const bool multi_line = UsesMultiLineEditor(type);

    current_type_ = type;

    if (multi_line)
    {
        multi_->ChangeValue(text);
    }
    else
    {
        single_->ChangeValue(text);
    }

    multi_->Show(multi_line);
    single_->Show(!multi_line);

    /* An untyped value is shown for reference but never rewritten. */
    multi_->SetEditable(type != appbox::RegistryValueType::None);

    hint_->SetLabel(HintFor(type));
    Layout();
}

bool RegistryValueDialog::CollectData(appbox::RegistryValueType type, std::vector<std::uint8_t>& data,
                                      std::string& error) const
{
    const auto text = UsesMultiLineEditor(type) ? multi_->GetValue() : single_->GetValue();
    return appbox::ParseRegistryValueText(type, text.ToStdWstring(), data, error);
}

void RegistryValueDialog::OnTypeChanged(wxCommandEvent& event)
{
    /*
     * The text of the previous type is parsed and shown again in the new
     * representation; a text which does not fit the previous type is dropped
     * instead of being carried over as garbage.
     */
    std::vector<std::uint8_t> data;
    std::string error;
    if (!CollectData(current_type_, data, error))
    {
        data.clear();
    }

    ShowEditorFor(Type(), data);
    event.Skip();
}

void RegistryValueDialog::OnOk(wxCommandEvent&)
{
    std::vector<std::uint8_t> data;
    std::string error;
    if (!CollectData(current_type_, data, error))
    {
        wxMessageBox(wxString::FromUTF8(error), GetTitle(), wxOK | wxICON_WARNING, this);
        return;
    }

    if (validator_ != nullptr)
    {
        const auto reason = validator_(Name(), Type(), data);
        if (!reason.empty())
        {
            wxMessageBox(wxString::FromUTF8(reason), GetTitle(), wxOK | wxICON_WARNING, this);
            return;
        }
    }

    data_ = std::move(data);
    EndModal(wxID_OK);
}
