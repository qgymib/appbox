#ifndef APPBOX_PACKER_WIDGET_FILESYSTEM_ISOLATION_DIALOG_HPP
#define APPBOX_PACKER_WIDGET_FILESYSTEM_ISOLATION_DIALOG_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include "core/FilesystemIsolationModel.hpp"

class wxCheckBox;
class wxRadioBox;

/**
 * @brief Dialog to pick the isolation mode of a folder of the filesystem view.
 *
 * The dialog is opened from the context menu of the filesystem tree, which
 * reaches every node of the view: a preset directory, an imported folder, a
 * folder below an import and the `Sandbox Filesystem` container, which is the
 * root of the view. It offers the modes a folder accepts and one option which
 * decides how far the chosen mode reaches: `Apply to all subfolders`
 * overwrites the folders below the node as well and is off by default, so a
 * mode which the user picks reaches the node itself only.
 */
class FilesystemIsolationDialog : public wxDialog
{
public:
    /**
     * @brief Create the isolation dialog of a folder.
     * @param[in] parent Parent window.
     * @param[in] target Description of the folder the dialog applies to, shown
     *                   as the first line of the dialog.
     * @param[in] initial Isolation mode selected when the dialog opens.
     */
    FilesystemIsolationDialog(wxWindow* parent, const wxString& target, appbox::FilesystemIsolation initial);

    /**
     * @brief Get the selected isolation mode.
     *
     * Only valid after ShowModal() returned wxID_OK.
     *
     * @return The selected mode.
     */
    appbox::FilesystemIsolation Isolation() const;

    /**
     * @brief Whether the mode has to be applied to the subfolders as well.
     * @return true when the folders below the node are overwritten.
     */
    bool ApplyToSubfolders() const;

private:
    /**
     * @brief Field holding the isolation modes.
     */
    wxRadioBox* isolation_ = nullptr;

    /**
     * @brief Option which overwrites the folders below the node.
     */
    wxCheckBox* subfolders_ = nullptr;
};

#endif // APPBOX_PACKER_WIDGET_FILESYSTEM_ISOLATION_DIALOG_HPP
