#ifndef APPBOX_PACKER_WIDGET_REGISTRY_ISOLATION_DIALOG_HPP
#define APPBOX_PACKER_WIDGET_REGISTRY_ISOLATION_DIALOG_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include "core/RegistryModel.hpp"

class IsolationModeGroup;
class wxCheckBox;

/**
 * @brief Dialog to pick the isolation mode of a registry key.
 *
 * The dialog is opened from the context menu of the registry tree. It offers
 * the isolation modes of a key, every mode with the explanation of what it does
 * below its name (see `IsolationModeGroup`), and one option which decides how
 * far the chosen mode reaches: without the recursion the mode reaches the key
 * alone, with it the whole subtree of the key follows the mode, the values of
 * the subtree included (see `RegistryModel::ApplyIsolationToSubtree`).
 *
 * The first line of the dialog names the target of the mode and is built by the
 * caller, because the container of the tree carries no mode of its own: it uses
 * the line to spell out that a mode picked for the container reaches the five
 * root keys, or the whole tree with the recursion. The dialog itself is the
 * same for every node.
 *
 * A value carries the two value modes alone, so the modes of a value are picked
 * in the `Isolation` column of the table instead of this dialog.
 */
class RegistryIsolationDialog : public wxDialog
{
public:
    /**
     * @brief Create the isolation dialog of a key.
     * @param[in] parent Parent window.
     * @param[in] target Text of the first line, which names what the mode
     *                   reaches.
     * @param[in] initial Isolation mode selected when the dialog opens.
     */
    RegistryIsolationDialog(wxWindow* parent, const wxString& target, appbox::RegistryKeyIsolation initial);

    /**
     * @brief Get the selected isolation mode.
     *
     * Only valid after ShowModal() returned wxID_OK.
     *
     * @return The selected mode.
     */
    appbox::RegistryKeyIsolation Isolation() const;

    /**
     * @brief Whether the mode has to be applied to the whole subtree.
     * @return true when the sub keys and the values are overwritten as well.
     */
    bool Recursive() const;

private:
    /**
     * @brief Field holding the isolation modes.
     */
    IsolationModeGroup* isolation_ = nullptr;

    /**
     * @brief Option which overwrites the subtree of the key.
     */
    wxCheckBox* recursive_ = nullptr;
};

#endif // APPBOX_PACKER_WIDGET_REGISTRY_ISOLATION_DIALOG_HPP
