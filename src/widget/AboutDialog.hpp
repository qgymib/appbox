#ifndef APPBOX_PACKER_WIDGET_ABOUT_DIALOG_HPP
#define APPBOX_PACKER_WIDGET_ABOUT_DIALOG_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include "core/AboutInfo.hpp"

/**
 * @brief Dialog which tells what the application is and what it was built from.
 *
 * The dialog is opened by `Help -> About`. It shows the one sentence summary of
 * the application, the version of the project, the time and the git revision of
 * the build and the third-party libraries the binary was linked against.
 *
 * All of these values are compiled into the binary, so the dialog only lays out
 * what `appbox::GetAboutInfo()` hands out: it never reads a version file, calls
 * git or touches the file system while it is open. The functional description
 * lives in the packer core as well (`appbox::kAboutSummary`), which keeps the
 * dialog free of data of its own.
 */
class AboutDialog : public wxDialog
{
public:
    /**
     * @brief Create the about dialog.
     * @param[in] parent Parent window; its icon is reused for the dialog.
     */
    explicit AboutDialog(wxWindow* parent);

private:
    /**
     * @brief Add the icon, the name and the one sentence summary of the dialog.
     * @param[in] sizer Sizer which receives the header.
     */
    void CreateHeader(wxSizer& sizer);

    /**
     * @brief Add the version, the build date and the git revision.
     * @param[in] sizer Sizer which receives the details.
     * @param[in] info Build information of the binary.
     */
    void CreateBuildDetails(wxSizer& sizer, const appbox::AboutInfo& info);

    /**
     * @brief Add the table of the linked third-party libraries.
     * @param[in] sizer Sizer which receives the table.
     * @param[in] info Build information of the binary.
     */
    void CreateDependencyList(wxSizer& sizer, const appbox::AboutInfo& info);

    /**
     * @brief Add one label and its value to the grid of the build details.
     * @param[in] grid Grid which receives the row.
     * @param[in] label Text of the label.
     * @param[in] value Text of the value.
     */
    void AddDetailRow(wxFlexGridSizer& grid, const wxString& label, const wxString& value);
};

#endif // APPBOX_PACKER_WIDGET_ABOUT_DIALOG_HPP
