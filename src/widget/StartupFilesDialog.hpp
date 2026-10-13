#ifndef APPBOX_PACKER_WIDGET_STARTUP_FILES_DIALOG_HPP
#define APPBOX_PACKER_WIDGET_STARTUP_FILES_DIALOG_HPP

#include "StartupTreeModel.hpp"
#include "core/PackModel.hpp"
#include <wx/dataview.h>
#include <wx/wx.h>
#include <atomic>
#include <thread>
#include <vector>

/** Event which reports the finished background scan of the startup file tree. */
wxDECLARE_EVENT(APPBOX_STARTUP_SCAN_FINISHED, wxThreadEvent);

/**
 * @brief Dialog to browse the startable files and manage the startup files.
 *
 * The dialog shows the startup file tree as a tree table with the columns
 * Name, Type, Auto Start and Trigger. Only the files the launcher can start are
 * offered, see `common/StartupFileType.hpp`, and only the folders on the way to
 * one of them: the tree holds the container, the preset directories, the
 * imported folders, the folders the user created and the startable files below
 * them, which is the shape of the filesystem workspace.
 *
 * The tree opens with every row expanded, so the folders of the sandbox view
 * and the startable files below them are in view at once; a row can be
 * collapsed by hand like in any other tree.
 *
 * Ticking the Auto Start box of a file makes it a startup file which the
 * sandbox starts on its own; clearing the box keeps it in the list but leaves
 * it to the `--X-AppBox-Startup` option of the sandbox to start it. The Trigger
 * column carries the name the option uses and defaults to the file name without
 * its extension.
 *
 * The tree reads the host folders of the imported folders, so it is built by a
 * background thread: the dialog is shown at once and reports that it is
 * scanning until the tree is there. The whole list is applied at once when the
 * dialog is confirmed, so the model never sees a partially edited list.
 */
class StartupFilesDialog : public wxDialog
{
public:
    /**
     * @brief Create the startup file browser and start the scan of its tree.
     * @param[in] parent Parent window.
     * @param[in] model The pack model holding the imports and the startup
     *                  files.
     */
    StartupFilesDialog(wxWindow* parent, const appbox::PackModel& model);

    /**
     * @brief Stop the background scan of the tree.
     *
     * The scan reports its result to this window, so it has to be joined before
     * the window is destroyed. A scan which runs while the user closes the
     * dialog is waited for, which keeps an event from being queued to a window
     * which no longer exists.
     */
    ~StartupFilesDialog() override;

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
     * @brief Take over the tree of the finished scan.
     * @param[in] event Thread event carrying the tree.
     */
    void OnScanFinished(wxThreadEvent& event);

    /**
     * @brief Expand every row of the tree and show the first startup file.
     *
     * The dialog opens with the whole tree in view, so the user browses the
     * folders of the sandbox view without opening them by hand. A row can be
     * collapsed again, the expansion is the state the dialog starts with.
     */
    void ExpandAll();

    /**
     * @brief Enable or disable the buttons for the current state.
     */
    void UpdateButtons();

    /**
     * @brief Close the dialog with the configured startup files.
     */
    void Accept();

    /**
     * @brief Toggle the automatic start of a file activated by double click or
     *        Enter.
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
    wxStaticText*                    status_ = nullptr;
    wxButton*                        ok_button_ = nullptr;
    wxButton*                        remove_button_ = nullptr;
    std::vector<appbox::StartupFile> selection_;

    /**
     * @brief Thread which builds the tree of the dialog.
     */
    std::thread scan_thread_;

    /**
     * @brief Whether the dialog was closed while the scan was still running.
     */
    std::atomic<bool> scan_cancelled_{ false };
};

#endif // APPBOX_PACKER_WIDGET_STARTUP_FILES_DIALOG_HPP
