#include "ModeChoiceGroup.hpp"
#include "core/FilesystemIsolationModel.hpp"
#include "core/RegistryModel.hpp"
#include <wx/radiobut.h>
#include <wx/settings.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

namespace
{

/**
 * @brief Width the explanation of an option is wrapped at.
 *
 * The explanations are full sentences, so they are wrapped to a fixed column
 * which keeps a dialog readable instead of growing to the length of the longest
 * text.
 */
constexpr int kDescriptionWidth = 380;

/** Indent of the explanation, which starts below the name of an option. */
constexpr int kDescriptionIndent = 20;

/** Gap between the name of an option and its explanation. */
constexpr int kLabelGap = 2;

/** Gap between two options. */
constexpr int kChoiceGap = 8;

/**
 * @brief Create the grey text which explains one mode.
 * @param[in] parent Parent window.
 * @param[in] description Text of the explanation.
 * @return The text control.
 */
wxStaticText* CreateDescription(wxWindow* parent, const wxString& description)
{
    auto* text = new wxStaticText(parent, wxID_ANY, description);
    text->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    text->Wrap(kDescriptionWidth);
    return text;
}

} // namespace

ModeChoiceGroup::ModeChoiceGroup(wxWindow* parent, const wxString& title, const std::vector<ModeChoice>& choices)
    : wxPanel(parent, wxID_ANY)
{
    auto* box = new wxStaticBoxSizer(wxVERTICAL, this, title);

    /*
     * The controls hang below the static box, which is what makes the label of
     * the box part of the group on every platform. The first radio button opens
     * the group, so the buttons below it select each other.
     */
    wxWindow* const holder = box->GetStaticBox();

    for (std::size_t index = 0; index < choices.size(); ++index)
    {
        const auto& choice = choices[index];

        const long style = index == 0 ? wxRB_GROUP : 0;
        auto*      button = new wxRadioButton(holder, wxID_ANY, choice.label, wxDefaultPosition, wxDefaultSize, style);
        button->Enable(choice.enabled);
        buttons_.push_back(button);

        /* The explanation hangs below its name and is indented to the name. */
        auto* option = new wxBoxSizer(wxVERTICAL);
        option->Add(button, 0, wxLEFT | wxRIGHT | wxBOTTOM, kLabelGap);
        option->Add(CreateDescription(holder, choice.description), 0, wxLEFT | wxRIGHT, kDescriptionIndent);

        /*
         * The gap which separates two options is vertical: a border is applied
         * to every side it names, so an option which carried the gap as a
         * border would also be moved to the right of the one above it, and the
         * options of a group would not line up. The option itself therefore
         * carries no border, and the gap is a spacer of its own.
         */
        if (index != 0)
        {
            box->AddSpacer(kChoiceGap);
        }

        box->Add(option, 0);
    }

    SetSizer(box);
}

int ModeChoiceGroup::Selection() const
{
    for (std::size_t index = 0; index < buttons_.size(); ++index)
    {
        if (buttons_[index]->GetValue())
        {
            return static_cast<int>(index);
        }
    }

    return wxNOT_FOUND;
}

void ModeChoiceGroup::SetSelection(int index)
{
    for (std::size_t position = 0; position < buttons_.size(); ++position)
    {
        buttons_[position]->SetValue(static_cast<int>(position) == index);
    }
}

std::vector<ModeChoice> RegistryKeyIsolationChoices()
{
    const auto& modes = appbox::RegistryKeyIsolationNames();

    std::vector<ModeChoice> choices;
    choices.reserve(modes.size());

    for (const auto& mode : modes)
    {
        const auto isolation = static_cast<appbox::RegistryKeyIsolation>(choices.size());
        choices.push_back({ wxString(mode), wxString(appbox::RegistryKeyIsolationDescription(isolation)) });
    }

    return choices;
}

std::vector<ModeChoice> RegistryValueIsolationChoices()
{
    const auto& modes = appbox::RegistryValueIsolationNames();

    std::vector<ModeChoice> choices;
    choices.reserve(modes.size());

    for (const auto& mode : modes)
    {
        const auto isolation = static_cast<appbox::RegistryValueIsolation>(choices.size());
        choices.push_back({ wxString(mode), wxString(appbox::RegistryValueIsolationDescription(isolation)) });
    }

    return choices;
}

std::vector<ModeChoice> FilesystemIsolationChoices()
{
    const auto& modes = appbox::FilesystemIsolationNamesFor(appbox::FilesystemEntryKind::Directory);

    std::vector<ModeChoice> choices;
    choices.reserve(modes.size());

    for (const auto& mode : modes)
    {
        const auto isolation = static_cast<appbox::FilesystemIsolation>(choices.size());
        choices.push_back({ wxString(mode), wxString(appbox::FilesystemIsolationDescription(
                                                isolation, appbox::FilesystemEntryKind::Directory)) });
    }

    return choices;
}
