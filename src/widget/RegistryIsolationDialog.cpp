#include "RegistryIsolationDialog.hpp"
#include "ModeChoiceGroup.hpp"
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace
{

/** Border of the dialog and gap between its parts. */
constexpr int kBorder = 12;

/**
 * @brief Width the first line is wrapped at.
 *
 * The line names what the mode reaches and the container spells its two scopes
 * out in it, so the text is wrapped to the column of the mode explanations
 * instead of growing the dialog to the length of the sentence.
 */
constexpr int kTargetWidth = 380;

} // namespace

RegistryIsolationDialog::RegistryIsolationDialog(wxWindow* parent, const wxString& target,
                                                 appbox::RegistryKeyIsolation initial)
    : wxDialog(parent, wxID_ANY, "Isolation Mode", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* target_text = new wxStaticText(this, wxID_ANY, target);
    target_text->Wrap(kTargetWidth);
    sizer->Add(target_text, 0, wxALL, kBorder);

    /*
     * The options are ordered like the enumeration of the modes, so the
     * selection of the field is the mode itself, and every option carries the
     * explanation of its mode below its name, see ModeChoiceGroup.
     */
    isolation_ = new ModeChoiceGroup(this, "Isolation", RegistryKeyIsolationChoices());
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
    const int selection = isolation_ != nullptr ? isolation_->Selection() : wxNOT_FOUND;
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
