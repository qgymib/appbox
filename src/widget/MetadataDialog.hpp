#ifndef APPBOX_PACKER_WIDGET_METADATA_DIALOG_HPP
#define APPBOX_PACKER_WIDGET_METADATA_DIALOG_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include "core/ApplicationMetadata.hpp"
#include <vector>

/**
 * @brief Dialog which edits every field of the file properties of a launcher.
 *
 * The `Metadata` tab of the Settings workspace shows the fields a user fills in
 * most of the time; this dialog offers the whole set of string fields a version
 * resource may carry, so a field the tab leaves out - the internal name, the
 * comments or the private build for example - is editable as well.
 *
 * The dialog is a plain editor: it opens with the values the tab currently
 * shows and hands the edited values back, so the caller decides what an edited
 * value means for the session. A field which is left empty is returned empty
 * and not dropped, because an empty field is what the tab shows as well.
 */
class MetadataDialog : public wxDialog
{
public:
    /**
     * @brief Create the dialog.
     * @param[in] parent Parent window.
     * @param[in] values Values the dialog opens with; a field the list does not
     *                   hold is shown empty.
     */
    MetadataDialog(wxWindow* parent, const std::vector<appbox::MetadataField>& values);

    /**
     * @brief Get the edited values.
     * @return The value of every field, in `MetadataFields()` order.
     */
    std::vector<appbox::MetadataField> Values() const;

private:
    /**
     * @brief Accept the values and close the dialog.
     * @param[in] event Command event of the OK button.
     */
    void OnOk(wxCommandEvent& event);

    /**
     * @brief The field boxes of the dialog, in `MetadataFields()` order.
     */
    std::vector<wxTextCtrl*> editors_;
};

#endif // APPBOX_PACKER_WIDGET_METADATA_DIALOG_HPP
