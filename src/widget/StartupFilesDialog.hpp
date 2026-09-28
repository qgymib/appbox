#ifndef APPBOX_PACKER_WIDGET_STARTUP_FILES_DIALOG_HPP
#define APPBOX_PACKER_WIDGET_STARTUP_FILES_DIALOG_HPP

#include "StartupTreeModel.hpp"
#include "core/PackModel.hpp"
#include <wx/dataview.h>
#include <wx/wx.h>
#include <vector>

/**
 * @brief Dialog to browse the imported folders and manage the startup files.
 *
 * The dialog shows the startup file tree as a tree table with the columns
 * Name, Type, Auto Start and Trigger. Ticking the Auto Start box of an
 * executable makes it a startup file which the sandbox starts on its own;
 * clearing the box keeps it in the list but leaves it to the
 * `--X-AppBox-Startup` option of the sandbox to start it. The Trigger column
 * carries the name the option uses and defaults to the file name of the
 * executable without its extension.
 *
 * The whole list is applied at once when the dialog is confirmed, so the
 * model never sees a partially edited list.
 */
class StartupFilesDialog : public wxDialog
{
public:
    /**
     * @brief Create the startup file browser.
     * @param[in] parent Parent window.
     * @param[in] model The pack model holding the imports and the startup
     *                  files.
     */
    StartupFilesDialog(wxWindow* parent, const appbox::PackModel& model);

    /**
     * @brief Get the startup files configured by the user.
     *
     * Only valid after ShowModal() returned wxID_OK.
     *
     * @return The startup files in startup order.
     */
    const std::vector<appbox::StartupFile>& Selection() const;

private:
    /**
     * @brief Show the current startup files and expand the tree to them.
     * @param[in] files The startup files to reveal.
     */
    void RevealSelection(const std::vector<appbox::StartupFile>& files);

    /**
     * @brief Enable or disable the buttons for the current state.
     */
    void UpdateButtons();

    /**
     * @brief Close the dialog with the configured startup files.
     */
    void Accept();

    /**
     * @brief Toggle the automatic start of an executable activated by double
     *        click or Enter.
     * @param[in] event Tree item activation event.
     */
    void OnItemActivated(wxDataViewEvent& event);

    /**
     * @brief Report a rejected value and restore the cell content.
     * @param[in] event Data view item value change event.
     */
    void OnValueChanged(wxDataViewEvent& event);

    /**
     * @brief Update the buttons after the selected row changed.
     * @param[in] event Data view selection change event.
     */
    void OnSelectionChanged(wxDataViewEvent& event);

    /**
     * @brief Drop the selected row from the startup file list.
     * @param[in] event Command event.
     */
    void OnRemove(wxCommandEvent& event);

    /**
     * @brief Validate the selection before closing with OK.
     * @param[in] event Command event.
     */
    void OnOk(wxCommandEvent& event);

    StartupTreeModel*                tree_model_ = nullptr;
    wxDataViewCtrl*                  tree_ = nullptr;
    wxButton*                        ok_button_ = nullptr;
    wxButton*                        remove_button_ = nullptr;
    std::vector<appbox::StartupFile> selection_;
};

#endif // APPBOX_PACKER_WIDGET_STARTUP_FILES_DIALOG_HPP
