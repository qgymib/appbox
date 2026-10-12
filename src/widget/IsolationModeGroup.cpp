#include "IsolationModeGroup.hpp"
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

IsolationModeGroup::IsolationModeGroup(wxWindow* parent, const std::vector<IsolationModeChoice>& choices)
    : wxPanel(parent, wxID_ANY)
{
    auto* box = new wxStaticBoxSizer(wxVERTICAL, this, "Isolation");

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
        buttons_.push_back(button);

        /* The explanation hangs below its name and is indented to the name. */
        auto* option = new wxBoxSizer(wxVERTICAL);
        option->Add(button, 0, wxLEFT | wxRIGHT | wxBOTTOM, kLabelGap);
        option->Add(CreateDescription(holder, choice.description), 0, wxLEFT | wxRIGHT, kDescriptionIndent);

        box->Add(option, 0, wxLEFT | wxRIGHT | wxTOP, index == 0 ? 0 : kChoiceGap);
    }

    SetSizer(box);
}

int IsolationModeGroup::Selection() const
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

void IsolationModeGroup::SetSelection(int index)
{
    for (std::size_t position = 0; position < buttons_.size(); ++position)
    {
        buttons_[position]->SetValue(static_cast<int>(position) == index);
    }
}

std::vector<IsolationModeChoice> RegistryKeyIsolationChoices()
{
    const auto& modes = appbox::RegistryKeyIsolationNames();

    std::vector<IsolationModeChoice> choices;
    choices.reserve(modes.size());

    for (const auto& mode : modes)
    {
        const auto isolation = static_cast<appbox::RegistryKeyIsolation>(choices.size());
        choices.push_back({ wxString(mode), wxString(appbox::RegistryKeyIsolationDescription(isolation)) });
    }

    return choices;
}

std::vector<IsolationModeChoice> RegistryValueIsolationChoices()
{
    const auto& modes = appbox::RegistryValueIsolationNames();

    std::vector<IsolationModeChoice> choices;
    choices.reserve(modes.size());

    for (const auto& mode : modes)
    {
        const auto isolation = static_cast<appbox::RegistryValueIsolation>(choices.size());
        choices.push_back({ wxString(mode), wxString(appbox::RegistryValueIsolationDescription(isolation)) });
    }

    return choices;
}

std::vector<IsolationModeChoice> FilesystemIsolationChoices()
{
    const auto& modes = appbox::FilesystemIsolationNamesFor(appbox::FilesystemEntryKind::Directory);

    std::vector<IsolationModeChoice> choices;
    choices.reserve(modes.size());

    for (const auto& mode : modes)
    {
        const auto isolation = static_cast<appbox::FilesystemIsolation>(choices.size());
        choices.push_back({ wxString(mode), wxString(appbox::FilesystemIsolationDescription(
                                                isolation, appbox::FilesystemEntryKind::Directory)) });
    }

    return choices;
}
