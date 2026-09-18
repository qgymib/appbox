#ifndef APPBOX_PACKER_WIDGET_MAIN_FRAME_HPP
#define APPBOX_PACKER_WIDGET_MAIN_FRAME_HPP

#include <wx/wx.h>
#include <atomic>
#include <string>
#include <thread>
#include "core/PackModel.hpp"

class FilesystemPanel;
class RibbonBar;
class SideNav;
class wxProgressDialog;
class wxSimplebook;

wxDECLARE_EVENT(APPBOX_PACK_PROGRESS, wxThreadEvent);
wxDECLARE_EVENT(APPBOX_PACK_FINISHED, wxThreadEvent);

/**
 * @brief Result of one background pack run.
 */
struct PackOutcome
{
    /**
     * @brief Error description, empty when the run succeeded.
     */
    std::string error;

    /**
     * @brief Folder the archive was extracted to, empty when no run was requested.
     */
    std::wstring extract_dir;
};

/**
 * @brief Main window of the packer.
 *
 * The window follows the three part layout of the reference packaging tool:
 * the ribbon bar on top, the vertical icon navigation on the left and the
 * workspace on the right. The pack run itself executes on a background thread
 * and reports back through thread events.
 */
class MainFrame : public wxFrame
{
public:
    /**
     * @brief Create the packer main window.
     */
    MainFrame();

    /**
     * @brief Join a still running pack thread and free the dialog.
     */
    ~MainFrame() override;

private:
    /**
     * @brief Create the menu bar.
     */
    void CreateMenuBar();

    /**
     * @brief Create the ribbon, the navigation and the workspace.
     */
    void CreateLayout();

    /**
     * @brief Apply the application icon to the window.
     *
     * wx registers its window classes without an icon and wxFrame has no
     * default icon either, so a frame is shown without any title bar icon
     * unless the icon embedded by resource.rc is applied explicitly.
     */
    void ApplyWindowIcon();

    /**
     * @brief Update the main program display of the status bar.
     */
    void UpdateStatusBar();

    /**
     * @brief Update the window title with the destination archive path.
     */
    void UpdateTitle();

    /**
     * @brief Get the destination archive path of the next pack run.
     * @return The path of the Output File box, or the derived default when the
     *         box is empty.
     */
    wxString OutputPath() const;

    /**
     * @brief Get the default archive path derived from the main program.
     * @return The default archive path.
     */
    wxString DefaultOutputPath() const;

    /**
     * @brief Mark the archive path as edited by the user.
     * @param[in] event Command event of the Output File box.
     */
    void OnOutputPathEdited(wxCommandEvent& event);

    /**
     * @brief Handle the activation of a navigation item.
     * @param[in] event Command event carrying the item index.
     */
    void OnSideNavChanged(wxCommandEvent& event);

    /**
     * @brief Handle the application exit command.
     * @param[in] event Command event.
     */
    void OnExit(wxCommandEvent& event);

    /**
     * @brief Show the about dialog.
     * @param[in] event Command event.
     */
    void OnAbout(wxCommandEvent& event);

    /**
     * @brief Open the main program browser and apply the selection.
     * @param[in] event Command event.
     */
    void OnSelectMainProgram(wxCommandEvent& event);

    /**
     * @brief Choose the destination archive path.
     * @param[in] event Command event.
     */
    void OnBrowseOutput(wxCommandEvent& event);

    /**
     * @brief Validate the model and start the pack thread.
     * @param[in] event Command event.
     */
    void OnBuild(wxCommandEvent& event);

    /**
     * @brief Pack the model and start the packaged application afterwards.
     * @param[in] event Command event.
     */
    void OnBuildAndRun(wxCommandEvent& event);

    /**
     * @brief Start the background pack run.
     *
     * The run reports its progress into a single progress dialog which also
     * presents the result of the run once it finished.
     *
     * @param[in] run_after Whether the archive is extracted and started after
     *                      the pack run finished.
     */
    void StartPack(bool run_after);

    /**
     * @brief Forward pack progress into the progress dialog.
     * @param[in] event Thread event carrying done and total file counts.
     */
    void OnPackProgress(wxThreadEvent& event);

    /**
     * @brief Finish the pack run and present the result inside the progress dialog.
     *
     * The dialog is reused: it keeps its progress bar and shows the result
     * text until the user closes it, so a build run needs a single dialog.
     *
     * @param[in] event Thread event carrying the pack outcome.
     */
    void OnPackFinished(wxThreadEvent& event);

    appbox::PackModel model_;

    RibbonBar*       ribbon_ = nullptr;
    SideNav*         side_nav_ = nullptr;
    wxSimplebook*    workspace_ = nullptr;
    FilesystemPanel* filesystem_panel_ = nullptr;

    bool output_path_edited_ = false;

    std::thread         pack_thread_;
    wxProgressDialog*   progress_dialog_ = nullptr;
    std::atomic<bool>   pack_cancelled_{false};
    bool                run_after_pack_ = false;

    /**
     * @brief Maximum of the progress dialog, which is also the value of its
     *        finished state.
     */
    int pack_range_ = 0;

    /**
     * @brief Whether the run finished and the dialog presents its result.
     */
    bool pack_finished_ = false;
};

#endif // APPBOX_PACKER_WIDGET_MAIN_FRAME_HPP
