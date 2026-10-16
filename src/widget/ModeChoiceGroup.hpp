#ifndef APPBOX_PACKER_WIDGET_MODE_CHOICE_GROUP_HPP
#define APPBOX_PACKER_WIDGET_MODE_CHOICE_GROUP_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include <vector>

class wxRadioButton;

/**
 * @brief One option of a mode choice group.
 */
struct ModeChoice
{
    /**
     * @brief Display name of the mode, for example `Merge`.
     */
    wxString label;

    /**
     * @brief Explanation of the mode, shown below the option.
     */
    wxString description;

    /**
     * @brief Whether the option can be picked.
     *
     * A mode the machine cannot run is drawn greyed out instead of being
     * offered, which keeps a choice the caller would refuse from looking like
     * one the user made.
     */
    bool enabled = true;
};

/**
 * @brief Group of radio buttons which pick a mode.
 *
 * The group replaces the `wxRadioBox` the dialogs used to offer: a radio box
 * draws the name of an option alone, while the group draws the name and, below
 * it, an indented grey text which explains the mode. The explanation of every
 * mode is visible at once, so the meaning of a mode does not have to be looked
 * up in a tooltip.
 *
 * The group knows nothing about the modes it offers: the title of the group and
 * the text of every option are provided by the caller, which takes them from
 * the model of the domain (see `RegistryKeyIsolationChoices()`,
 * `RegistryValueIsolationChoices()`, `FilesystemIsolationChoices()` and
 * `appbox::CaptureModeDescription()`), so the explanation of a mode lives in
 * one place.
 *
 * A radio button raises `wxEVT_RADIOBUTTON`, which is a command event and
 * therefore reaches the parent of the group: a caller which has to act on the
 * choice as soon as it is made binds that event and reads Selection().
 */
class ModeChoiceGroup : public wxPanel
{
public:
    /**
     * @brief Create the group.
     * @param[in] parent Parent window.
     * @param[in] title Title of the box which frames the options.
     * @param[in] choices Options of the group, in the order they are shown.
     */
    ModeChoiceGroup(wxWindow* parent, const wxString& title, const std::vector<ModeChoice>& choices);

    /**
     * @brief Get the index of the selected option.
     * @return The index of the option, wxNOT_FOUND when none is selected.
     */
    int Selection() const;

    /**
     * @brief Select one option.
     *
     * An index outside the options is ignored, so a caller may pass the index
     * of a mode without checking it first.
     *
     * @param[in] index Index of the option.
     */
    void SetSelection(int index);

private:
    /**
     * @brief Radio buttons of the group, in the order of the choices.
     */
    std::vector<wxRadioButton*> buttons_;
};

/**
 * @brief Build the options of the isolation modes of a key.
 *
 * The options follow the order of `RegistryKeyIsolationNames()`, so the index
 * of an option is the mode itself, and every option carries the text of
 * `RegistryKeyIsolationDescription()`.
 *
 * @return The four key modes with the explanation of each one.
 */
std::vector<ModeChoice> RegistryKeyIsolationChoices();

/**
 * @brief Build the options of the isolation modes of a value.
 *
 * The options follow the order of `RegistryValueIsolationNames()`, so the index
 * of an option is the mode itself, and every option carries the text of
 * `RegistryValueIsolationDescription()`.
 *
 * @return The two value modes with the explanation of each one.
 */
std::vector<ModeChoice> RegistryValueIsolationChoices();

/**
 * @brief Build the options of the isolation modes of a folder of the filesystem view.
 *
 * The options follow the order of
 * `FilesystemIsolationNamesFor(FilesystemEntryKind::Directory)`, which is the
 * order of the enumeration of the modes, so the index of an option is the mode
 * itself, and every option carries the text of
 * `FilesystemIsolationDescription()`. The name of a mode is the label of the
 * option above the text, so the text does not repeat it.
 *
 * @return The four folder modes with the explanation of each one.
 */
std::vector<ModeChoice> FilesystemIsolationChoices();

#endif // APPBOX_PACKER_WIDGET_MODE_CHOICE_GROUP_HPP
