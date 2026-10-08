#include "MetadataDialog.hpp"
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <cstddef>

namespace
{

/** Border of the form of the dialog. */
constexpr int kFormBorder = 12;

/** Gap between two rows of the form. */
constexpr int kRowGap = 8;

/** Gap between the label column and the editor column. */
constexpr int kFieldGap = 8;

/** Width of an editor of the form. */
constexpr int kFieldWidth = 360;

/** Minimum width of the dialog. */
constexpr int kDialogWidth = 560;

} // namespace

MetadataDialog::MetadataDialog(wxWindow* parent, const std::vector<appbox::MetadataField>& values)
    : wxDialog(parent, wxID_ANY, "File Properties", wxDefaultPosition, wxDefaultSize)
{
    const auto& fields = appbox::MetadataFields();

    auto* grid = new wxFlexGridSizer(static_cast<int>(fields.size()), 2, kRowGap, kFieldGap);
    grid->AddGrowableCol(1, 1);

    for (const auto& key : fields)
    {
        const auto* value = appbox::FindMetadataValue(values, key);

        auto* label = new wxStaticText(this, wxID_ANY, appbox::MetadataFieldLabel(key));
        grid->Add(label, 0, wxALIGN_CENTER_VERTICAL);

        auto* editor = new wxTextCtrl(this, wxID_ANY, value != nullptr ? wxString(*value) : wxString(),
                                      wxDefaultPosition, wxSize(kFieldWidth, -1));
        editors_.push_back(editor);
        grid->Add(editor, 1, wxEXPAND);
    }

    auto* buttons = new wxStdDialogButtonSizer();
    buttons->AddButton(new wxButton(this, wxID_OK));
    buttons->AddButton(new wxButton(this, wxID_CANCEL));
    buttons->Realize();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(grid, 1, wxEXPAND | wxALL, kFormBorder);
    sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kFormBorder);

    SetSizerAndFit(sizer);
    SetMinSize(wxSize(kDialogWidth, GetSize().GetHeight()));

    if (!editors_.empty())
    {
        editors_.front()->SetFocus();
    }

    Bind(wxEVT_BUTTON, &MetadataDialog::OnOk, this, wxID_OK);
}

std::vector<appbox::MetadataField> MetadataDialog::Values() const
{
    const auto& fields = appbox::MetadataFields();

    std::vector<appbox::MetadataField> values;
    values.reserve(editors_.size());

    for (std::size_t index = 0; index < editors_.size() && index < fields.size(); ++index)
    {
        values.push_back(appbox::MetadataField{ fields[index], editors_[index]->GetValue().ToStdWstring() });
    }

    return values;
}

void MetadataDialog::OnOk(wxCommandEvent&)
{
    /* Every field is a plain text, so there is nothing to validate. */
    EndModal(wxID_OK);
}
