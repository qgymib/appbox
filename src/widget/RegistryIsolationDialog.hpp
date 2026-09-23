#ifndef APPBOX_PACKER_WIDGET_REGISTRY_ISOLATION_DIALOG_HPP
#define APPBOX_PACKER_WIDGET_REGISTRY_ISOLATION_DIALOG_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include "core/RegistryModel.hpp"

class wxCheckBox;
class wxRadioBox;

/**
 * @brief Dialog to pick the isolation mode of a registry key.
 *
 * The dialog is opened from the context menu of the registry tree. It offers
 * the isolation modes of the workspace and two options which decide how far
 * the chosen mode reaches: `Apply to all sub keys` overwrites the whole
 * subtree of the key, `Apply to all values in the subtree` overwrites the
 * values below it as well. The value option only makes sense together with the
 * sub key option, so it is disabled while the sub keys are left alone.
 */
class RegistryIsolationDialog : public wxDialog
{
public:
    /**
     * @brief Create the isolation dialog of a key.
     * @param[in] parent Parent window.
     * @param[in] key_path Path of the key the dialog applies to.
     * @param[in] initial Isolation mode selected when the dialog opens.
     */
    RegistryIsolationDialog(wxWindow* parent, const wxString& key_path, appbox::RegistryIsolation initial);

    /**
     * @brief Get the selected isolation mode.
     *
     * Only valid after ShowModal() returned wxID_OK.
     *
     * @return The selected mode.
     */
    appbox::RegistryIsolation Isolation() const;

    /**
     * @brief Whether the mode has to be applied to the whole subtree.
     * @return true when the sub keys are overwritten as well.
     */
    bool ApplyToSubKeys() const;

    /**
     * @brief Whether the values below the key are overwritten as well.
     *
     * The option only counts together with ApplyToSubKeys(), which is the state
     * the dialog keeps while the option is disabled.
     *
     * @return true when the values are overwritten.
     */
    bool ApplyToValues() const;

private:
    /**
     * @brief Follow the state of the sub key option.
     * @param[in] event Command event of the sub key option.
     */
    void OnSubKeysToggled(wxCommandEvent& event);

    /**
     * @brief Field holding the isolation modes.
     */
    wxRadioBox* isolation_ = nullptr;

    /**
     * @brief Option which overwrites the sub keys of the key.
     */
    wxCheckBox* sub_keys_ = nullptr;

    /**
     * @brief Option which overwrites the values below the key.
     */
    wxCheckBox* values_ = nullptr;
};

#endif // APPBOX_PACKER_WIDGET_REGISTRY_ISOLATION_DIALOG_HPP
