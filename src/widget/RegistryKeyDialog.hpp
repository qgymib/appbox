#ifndef APPBOX_PACKER_WIDGET_REGISTRY_KEY_DIALOG_HPP
#define APPBOX_PACKER_WIDGET_REGISTRY_KEY_DIALOG_HPP

#include <wx/wx.h>
#include "core/RegistryModel.hpp"
#include <functional>
#include <string>

class wxRadioBox;
class wxTextCtrl;

/**
 * @brief Dialog to name a registry key and to pick its isolation mode.
 *
 * The dialog is used for both editing an existing key and creating a new one:
 * it shows the path of the parent key, a single name field, the isolation modes
 * a key accepts and the standard buttons. The name is validated by the caller
 * through a callback, so the rules of the registry model are applied without
 * duplicating them here; a rejected name keeps the dialog open and shows the
 * reason.
 *
 * The isolation mode is picked in the dialog as well. The dialog only reports
 * the chosen mode, the caller stores it through the model, so the modes of a
 * key are offered by one source: the names of the field are the ones the
 * `Isolation` column of the table and the isolation dialog of the tree show.
 */
class RegistryKeyDialog : public wxDialog
{
public:
    /**
     * @brief Validation callback of the dialog.
     *
     * @param[in] name Name entered by the user.
     * @return An empty text when the name is accepted, otherwise the reason.
     */
    using Validator = std::function<std::string(const std::wstring& name)>;

    /**
     * @brief Create the key dialog.
     * @param[in] parent Parent window.
     * @param[in] title Title of the dialog.
     * @param[in] parent_path Path of the parent key, empty for the container.
     * @param[in] initial_name Name shown in the name field.
     * @param[in] initial_isolation Isolation mode selected when the dialog opens.
     * @param[in] validator Validation callback of the entered name.
     */
    RegistryKeyDialog(wxWindow* parent, const wxString& title, const wxString& parent_path,
                      const wxString& initial_name, appbox::RegistryKeyIsolation initial_isolation,
                      Validator validator);

    /**
     * @brief Get the accepted name.
     *
     * Only valid after ShowModal() returned wxID_OK.
     *
     * @return The name entered by the user.
     */
    std::wstring Name() const;

    /**
     * @brief Get the selected isolation mode.
     *
     * Only valid after ShowModal() returned wxID_OK.
     *
     * @return The mode selected in the isolation field.
     */
    appbox::RegistryKeyIsolation Isolation() const;

private:
    /**
     * @brief Validate the name and close the dialog when it is accepted.
     * @param[in] event Command event of the OK button.
     */
    void OnOk(wxCommandEvent& event);

    /**
     * @brief Callback validating the entered name.
     */
    Validator validator_;

    /**
     * @brief Field holding the name of the key.
     */
    wxTextCtrl* name_ = nullptr;

    /**
     * @brief Field holding the isolation modes of a key.
     */
    wxRadioBox* isolation_ = nullptr;
};

#endif // APPBOX_PACKER_WIDGET_REGISTRY_KEY_DIALOG_HPP
