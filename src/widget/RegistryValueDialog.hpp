#ifndef APPBOX_PACKER_WIDGET_REGISTRY_VALUE_DIALOG_HPP
#define APPBOX_PACKER_WIDGET_REGISTRY_VALUE_DIALOG_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include "core/RegistryModel.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class wxChoice;
class wxStaticText;
class wxTextCtrl;

/**
 * @brief Dialog to edit the name, the type and the data of a registry value.
 *
 * The dialog is used for both editing an existing value and creating a new
 * one. The data editor follows the selected type: a single line field for
 * strings and numbers, a multi line field for multi strings and hexadecimal
 * bytes, and a read only field for untyped values, whose data is preserved
 * but never rewritten.
 *
 * The data is parsed and the name is validated through a callback, so the
 * rules of the registry model are applied without duplicating them here. A
 * rejected input keeps the dialog open and shows the reason.
 */
class RegistryValueDialog : public wxDialog
{
public:
    /**
     * @brief Validation callback of the dialog.
     *
     * @param[in] name Name entered by the user, empty for the default value.
     * @param[in] type Selected type.
     * @param[in] data Parsed raw data of the value.
     * @return An empty text when the value is accepted, otherwise the reason.
     */
    using Validator = std::function<std::string(const std::wstring& name, appbox::RegistryValueType type,
                                                const std::vector<std::uint8_t>& data)>;

    /**
     * @brief Create the value dialog.
     * @param[in] parent Parent window.
     * @param[in] title Title of the dialog.
     * @param[in] parent_path Path of the key holding the value.
     * @param[in] initial_name Name shown in the name field.
     * @param[in] initial_type Type selected in the type field.
     * @param[in] initial_data Data shown by the data editor.
     * @param[in] validator Validation callback of the entered value.
     */
    RegistryValueDialog(wxWindow* parent, const wxString& title, const wxString& parent_path,
                        const wxString& initial_name, appbox::RegistryValueType initial_type,
                        const std::vector<std::uint8_t>& initial_data, Validator validator);

    /**
     * @brief Get the accepted name.
     * @return The name entered by the user, empty for the default value.
     */
    std::wstring Name() const;

    /**
     * @brief Get the selected type.
     * @return The type selected in the type field.
     */
    appbox::RegistryValueType Type() const;

    /**
     * @brief Get the parsed data of the value.
     *
     * Only valid after ShowModal() returned wxID_OK.
     *
     * @return The raw data of the value.
     */
    const std::vector<std::uint8_t>& Data() const;

private:
    /**
     * @brief Show the editor which fits a type and fill it with data.
     * @param[in] type Type to show the editor for.
     * @param[in] data Data to show.
     */
    void ShowEditorFor(appbox::RegistryValueType type, const std::vector<std::uint8_t>& data);

    /**
     * @brief Read the active editor into raw value data.
     * @param[in] type Type the text is parsed with.
     * @param[out] data The parsed raw data.
     * @param[out] error Error description when the text does not fit the type.
     * @return true when the text was parsed.
     */
    bool CollectData(appbox::RegistryValueType type, std::vector<std::uint8_t>& data,
                     std::string& error) const;

    /**
     * @brief Follow a type change of the type field.
     * @param[in] event Command event of the type field.
     */
    void OnTypeChanged(wxCommandEvent& event);

    /**
     * @brief Validate the value and close the dialog when it is accepted.
     * @param[in] event Command event of the OK button.
     */
    void OnOk(wxCommandEvent& event);

    /**
     * @brief Callback validating the entered value.
     */
    Validator validator_;

    /**
     * @brief Field holding the name of the value.
     */
    wxTextCtrl* name_ = nullptr;

    /**
     * @brief Field holding the type of the value.
     */
    wxChoice* type_ = nullptr;

    /**
     * @brief Editor of the single line types.
     */
    wxTextCtrl* single_ = nullptr;

    /**
     * @brief Editor of the multi line types.
     */
    wxTextCtrl* multi_ = nullptr;

    /**
     * @brief Hint describing the input the selected type expects.
     */
    wxStaticText* hint_ = nullptr;

    /**
     * @brief Type the editors currently hold.
     *
     * The text of an editor is written in the representation of this type, so
     * it is parsed with it when the user switches to another type.
     */
    appbox::RegistryValueType current_type_ = appbox::RegistryValueType::String;

    /**
     * @brief Parsed data accepted by the last OK press.
     */
    std::vector<std::uint8_t> data_;
};

#endif // APPBOX_PACKER_WIDGET_REGISTRY_VALUE_DIALOG_HPP
