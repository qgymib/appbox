#ifndef APPBOX_PACKER_WIDGET_MAIN_FRAME_HPP
#define APPBOX_PACKER_WIDGET_MAIN_FRAME_HPP

#include <wx/wx.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include "core/BuildReport.hpp"
#include "core/FilesystemIsolationModel.hpp"
#include "core/PackModel.hpp"
#include "core/RegistryModel.hpp"

class FilesystemPanel;
class RegistryPanel;
class RibbonBar;
class SideNav;
class wxProgressDialog;
class wxSimplebook;
class wxTimer;
class wxTimerEvent;

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
     * @brief Path of the archive the run wrote, empty when it wrote none.
     */
    std::wstring archive_path;

    /**
     * @brief Folder the archive was extracted to, empty when no run was requested.
     */
    std::wstring extract_dir;

    /**
     * @brief File name of the loader program inside the archive.
     *
     * The loader carries the file name of the main program, so the extracted
     * archive has to be started through this name instead of a fixed one.
     */
    std::wstring loader_entry;
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
     *
     * The dialog presents the information which was compiled into the binary:
     * the one sentence summary of the application, its version, the date and
     * the git revision of the build and the versions of the linked third-party
     * libraries.
     *
     * @param[in] event Command event.
     */
    void OnAbout(wxCommandEvent& event);

    /**
     * @brief Import a configuration from a project file.
     *
     * The current configuration is replaced by the content of the file. A
     * non empty configuration is only dropped after the user confirmed the
     * replacement; a file which cannot be read leaves the configuration
     * untouched and reports the reason instead.
     *
     * @param[in] event Command event.
     */
    void OnImportConfiguration(wxCommandEvent& event);

    /**
     * @brief Export the current configuration into a project file.
     * @param[in] event Command event.
     */
    void OnExportConfiguration(wxCommandEvent& event);

    /**
     * @brief Merge a `.reg` file into the registry workspace.
     *
     * The file is parsed first and only applied when it is valid as a whole,
     * so a broken file leaves the registry untouched and reports the reason
     * instead.
     *
     * @param[in] event Command event.
     */
    void OnImportRegistry(wxCommandEvent& event);

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
     *
     * The report names the file which is being handled, so the dialog shows
     * what the run is working on instead of only counting files.
     *
     * @param[in] event Thread event carrying the progress report.
     */
    void OnPackProgress(wxThreadEvent& event);

    /**
     * @brief Refresh the elapsed time of the progress dialog.
     *
     * A single large file keeps the worker busy without producing a new
     * report, so the time shown by the dialog is refreshed by a timer as well.
     *
     * @param[in] event Timer event of the progress timer.
     */
    void OnProgressTimer(wxTimerEvent& event);

    /**
     * @brief Show the last progress report and the current elapsed time.
     *
     * Both a progress report of the worker thread and the progress timer end
     * up here, so the text of the dialog is composed in a single place.
     */
    void RefreshProgressDialog();

    /**
     * @brief Show a message inside the progress dialog.
     *
     * The native task dialog sizes itself from the message it was created with
     * and keeps that size when a later message is longer, which pushes the
     * buttons at its bottom out of the visible area. The dialog is therefore
     * re-fitted as soon as a message needs more room than every message before
     * it; the room needed only grows, so the dialog does not resize back and
     * forth while the run is going on.
     *
     * @param[in] value Progress value to show.
     * @param[in] text Message text.
     */
    void UpdateProgressDialog(int value, const wxString& text);

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

    /**
     * @brief Registry the packaged application will see.
     *
     * The model is filled by `File -> Import Registry` and by the registry
     * workspace itself, and it travels with the project file.
     */
    appbox::RegistryModel registry_model_;

    /**
     * @brief Isolation modes of the virtual filesystem.
     *
     * The model is filled by the isolation dropdown of the filesystem
     * workspace and travels with the project file.
     */
    appbox::FilesystemIsolationModel filesystem_isolation_;

    RibbonBar*       ribbon_ = nullptr;
    SideNav*         side_nav_ = nullptr;
    wxSimplebook*    workspace_ = nullptr;
    FilesystemPanel* filesystem_panel_ = nullptr;
    RegistryPanel*   registry_panel_ = nullptr;

    bool output_path_edited_ = false;

    std::thread         pack_thread_;
    wxProgressDialog*   progress_dialog_ = nullptr;
    wxTimer*            progress_timer_ = nullptr;
    std::atomic<bool>   pack_cancelled_{false};
    bool                run_after_pack_ = false;

    /**
     * @brief Time the running pack thread started, used for the elapsed time
     *        shown by the progress dialog.
     */
    std::chrono::steady_clock::time_point pack_start_;

    /**
     * @brief Number of files the packing stage reports.
     *
     * The extracting stage of a `Build and Run` run continues the progress bar
     * where the packing stage ended, so its values are offset by this count.
     */
    std::size_t pack_phase_total_ = 0;

    /**
     * @brief Maximum of the progress dialog, which is also the value of its
     *        finished state.
     */
    int pack_range_ = 0;

    /**
     * @brief Last report of the running stage.
     *
     * The timer reuses it to refresh the elapsed time without a new report.
     */
    appbox::BuildProgress last_progress_;

    /**
     * @brief Room the messages shown by the progress dialog needed so far.
     *
     * The native task dialog keeps the size it was created with, so a message
     * which needs more room than every message before it has to re-fit the
     * dialog, see UpdateProgressDialog().
     */
    std::size_t progress_fitted_lines_ = 0;
    std::size_t progress_fitted_width_ = 0;

    /**
     * @brief Whether the run finished and the dialog presents its result.
     */
    bool pack_finished_ = false;
};

#endif // APPBOX_PACKER_WIDGET_MAIN_FRAME_HPP
