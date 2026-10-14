#ifndef APPBOX_PACKER_WIDGET_MAIN_FRAME_HPP
#define APPBOX_PACKER_WIDGET_MAIN_FRAME_HPP

#include <wx/wx.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>
#include "core/ApplicationMetadata.hpp"
#include "core/BuildReport.hpp"
#include "core/EnvironmentModel.hpp"
#include "core/FilesystemIsolationModel.hpp"
#include "core/NetworkModel.hpp"
#include "core/PackModel.hpp"
#include "core/ProjectType.hpp"
#include "core/RegistryModel.hpp"
#include "core/SnapshotApply.hpp"
#include "core/SnapshotCapture.hpp"
#include "core/SnapshotStore.hpp"
#include "core/StartupScreen.hpp"

class ActionDialog;
class DebugPanel;
class EnvironmentPanel;
class FilesystemPanel;
class NetworkPanel;
class RegistryPanel;
class SettingsPanel;
class SideNav;
class Toolbar;
class wxSimplebook;

wxDECLARE_EVENT(APPBOX_PACK_PROGRESS, wxThreadEvent);
wxDECLARE_EVENT(APPBOX_PACK_FINISHED, wxThreadEvent);
wxDECLARE_EVENT(APPBOX_CAPTURE_PROGRESS, wxThreadEvent);
wxDECLARE_EVENT(APPBOX_CAPTURE_FINISHED, wxThreadEvent);

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
     * @brief File name of the launcher program inside the archive.
     *
     * The launcher carries the file name of the main program, so the extracted
     * archive has to be started through this name instead of a fixed one.
     */
    std::wstring launcher_entry;
};

/**
 * @brief Result of one background capture run.
 *
 * The run reads the state of the machine into one phase of a snapshot file,
 * and a run which compares the state with the baseline of the session applies
 * the difference to copies of the models, which the window adopts when the run
 * is over. The copies keep the background thread from reading or writing the
 * models the workspace shows.
 */
struct CaptureOutcome
{
    /**
     * @brief Error description, empty when the run succeeded.
     */
    std::string error;

    /**
     * @brief Whether the difference was applied to the models below.
     */
    bool applied = false;

    /**
     * @brief The pack model with the difference applied, see applied.
     */
    appbox::PackModel model;

    /**
     * @brief The registry with the difference applied, see applied.
     */
    appbox::RegistryModel registry;

    /**
     * @brief The environment with the difference applied, see applied.
     */
    appbox::EnvironmentModel environment;

    /**
     * @brief Number of the entries the run wrote into the phase, all zero for
     *        a run which failed.
     */
    appbox::SnapshotPhaseCounts counts;

    /**
     * @brief Summary of the difference and of what it did, empty for a
     *        baseline run.
     */
    std::wstring summary;
};

/**
 * @brief Main window of the packer.
 *
 * The window follows the three part layout of the reference packaging tool:
 * the toolbar on top, the vertical icon navigation on the left and the
 * workspace on the right. The pack run itself executes on a background thread
 * and reports back through thread events, and so does the capture of the state
 * of the machine, which the two capture commands of the toolbar start.
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
     * @brief Create the toolbar, the navigation and the workspace.
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
     * @brief Adopt the project type the Settings workspace shows.
     * @param[in] event Command event of the project type box.
     */
    void OnProjectTypeChanged(wxCommandEvent& event);

    /**
     * @brief Apply a project type to the session.
     *
     * The type decides which product the `Build` command writes and whether
     * the `Build and Run` command is offered, so the box, the buttons and the
     * status bar are refreshed from a single place.
     *
     * @param[in] type Project type to adopt.
     */
    void ApplyProjectType(appbox::ProjectType type);

    /**
     * @brief Refresh the Metadata tab of the Settings workspace.
     *
     * The tab offers the programs the file properties can be inherited from,
     * which are the startup files of the session, and shows the values of the
     * program the session reads from with the fields the user edited applied
     * on top of them. The program is read again on every call, so a source
     * which was updated on disk is picked up without reopening the project.
     *
     * A patch project writes no launcher, so the tab is disabled and explains
     * why instead of offering a source which would never be packed.
     */
    void RefreshMetadata();

    /**
     * @brief Adopt the file properties the Metadata tab shows.
     *
     * The tab cannot tell a field which follows the source program from a
     * field the user edited, so the session derives the overrides from the
     * values of the tab and the values which were read from the source.
     *
     * @param[in] event Command event of the tab.
     */
    void OnMetadataChanged(wxCommandEvent& event);

    /**
     * @brief Choose any program the file properties are inherited from.
     * @param[in] event Command event of the `Browse...` button.
     */
    void OnBrowseMetadata(wxCommandEvent& event);

    /**
     * @brief Refresh the Startup tab of the Settings workspace.
     *
     * The tab shows the startup screen of the session and a note which
     * describes what the archive will present. A patch project writes no
     * launcher, so the tab is disabled and explains why instead of offering a
     * setting which would never be packed.
     */
    void RefreshStartupScreen();

    /**
     * @brief Adopt the startup screen the Startup tab shows.
     * @param[in] event Command event of the tab.
     */
    void OnStartupScreenChanged(wxCommandEvent& event);

    /**
     * @brief Choose the image the startup screen presents.
     * @param[in] event Command event of the `Browse...` button.
     */
    void OnBrowseStartupImage(wxCommandEvent& event);

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
     * @brief Keep the state of the machine as the baseline of the session.
     *
     * The command asks for the snapshot file to write first and reads the
     * folders of the preset directories, the registry and the environment of
     * the machine into it, which is the state the next comparison uses, see
     * OnCaptureAndDiff().
     *
     * @param[in] event Command event.
     */
    void OnCaptureBefore(wxCommandEvent& event);

    /**
     * @brief Compare the state of the machine with the baseline of the session.
     *
     * The state the machine is in now is written into the phase after the
     * change of the snapshot file which holds the baseline, and the difference
     * of the two phases is applied to the filesystem, the registry and the
     * environment workspace, so the session describes the change the machine
     * went through and the archive carries it.
     *
     * The comparison does not become the baseline of the next one: the file
     * holds a state which the machine has already left behind, so a new
     * `Capture Before` is required.
     *
     * @param[in] event Command event.
     */
    void OnCaptureAndDiff(wxCommandEvent& event);

    /**
     * @brief Save the baseline of the session into another snapshot file.
     *
     * The command writes the state before the change of the file the session
     * uses into a file the user picks, so a baseline can be kept and loaded
     * later on; the session keeps working with its own file.
     *
     * @param[in] event Command event.
     */
    void OnSaveSnapshotAs(wxCommandEvent& event);

    /**
     * @brief Load a snapshot file and use it as the baseline.
     *
     * Only the path of the file is kept: the state before the change of the
     * file is the baseline and the comparison writes the state after the
     * change into the same file, which is what lets a session compare the
     * machine with a state an earlier run captured.
     *
     * @param[in] event Command event.
     */
    void OnLoadSnapshot(wxCommandEvent& event);

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
     * @brief Open the startup file browser and apply the selection.
     * @param[in] event Command event.
     */
    void OnStartupFiles(wxCommandEvent& event);

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
     * The run reports its progress into a single action dialog which also
     * presents the result of the run once it finished.
     *
     * @param[in] run_after Whether the archive is extracted and started after
     *                      the pack run finished.
     */
    void StartPack(bool run_after);

    /**
     * @brief Forward pack progress into the action dialog.
     *
     * The report names the file which is being handled, so the dialog shows
     * what the run is working on instead of only counting files.
     *
     * @param[in] event Thread event carrying the progress report.
     */
    void OnPackProgress(wxThreadEvent& event);

    /**
     * @brief Show the last pack report in the action dialog.
     *
     * The dialog keeps the elapsed time on its own clock, so a report only
     * updates the action, the object, the file counter and the progress bar.
     */
    void ApplyPackProgress();

    /**
     * @brief Finish the pack run and present the result inside the action dialog.
     *
     * The dialog is reused: it keeps its progress bar and shows the result
     * in its fields until the user closes it, so a build run needs a single
     * dialog.
     *
     * @param[in] event Thread event carrying the pack outcome.
     */
    void OnPackFinished(wxThreadEvent& event);

    /**
     * @brief Start the background capture run.
     *
     * @param[in] diff Whether the run compares the state with the baseline of
     *                 the session and writes the difference into the
     *                 workspaces; a run without it writes the baseline.
     * @param[in] path Path of the snapshot file the run works with: the file
     *                 which is written as the baseline, or the file which
     *                 holds the baseline and receives the state after the
     *                 change.
     */
    void StartCapture(bool diff, const std::wstring& path);

    /**
     * @brief Forward capture progress into the action dialog.
     * @param[in] event Thread event carrying the progress report.
     */
    void OnCaptureProgress(wxThreadEvent& event);

    /**
     * @brief Show the last capture report in the action dialog.
     *
     * The dialog keeps the elapsed time and the cycle of its bar on its own
     * clock, so a report only updates the action, the object and the entry
     * counter.
     */
    void ApplyCaptureProgress();

    /**
     * @brief Finish the capture run and present the result.
     *
     * The result of a baseline run is the file which was written and the
     * counts of the state it holds, the result of a comparison is the summary
     * of the difference which was written into the workspaces. The window
     * keeps the file of a baseline as the baseline of the session and drops
     * the one of a comparison, which the user takes again before the next one.
     *
     * @param[in] event Thread event carrying the capture outcome.
     */
    void OnCaptureFinished(wxThreadEvent& event);

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

    /**
     * @brief DNS redirections of the network workspace.
     *
     * The model is filled by the network workspace itself, it travels with the
     * project file and it is written into the archive, where the sandbox
     * answers the name resolution of the packaged application from it.
     */
    appbox::NetworkModel network_;

    /**
     * @brief Environment variables of the environment workspace.
     *
     * The model is filled by the environment workspace itself, it travels with
     * the project file and it is written into the archive, where the sandbox
     * composes the environment of the packaged application from it.
     */
    appbox::EnvironmentModel environment_;

    /**
     * @brief File properties the launcher of a standalone archive carries.
     *
     * The session stores the program the information is inherited from and the
     * fields the user edited only; the fields which were not touched are read
     * from that program again on every pack run. The information travels with
     * the project file.
     */
    appbox::ApplicationMetadata metadata_;

    /**
     * @brief The fields the Metadata tab read from the source program.
     *
     * The list is the state the tab shows before the overrides of the session
     * are applied to it, so an edit of a field can be told apart from a value
     * which still follows the source program.
     */
    std::vector<appbox::MetadataField> metadata_inherited_;

    /**
     * @brief Startup screen the launcher of a standalone archive shows.
     *
     * The session stores whether the screen is shown at all and the host path
     * of the image it presents; the pack run reads that file and writes it into
     * the archive. The setting travels with the project file.
     */
    appbox::StartupScreen startup_screen_;

    /**
     * @brief Kind of product the `Build` command writes.
     *
     * The type is picked in the `Project Type` box of the Settings workspace
     * and travels with the project file: a standalone project is packed into a
     * self-contained archive, a patch project into a package of the resources
     * without a launcher.
     */
    appbox::ProjectType project_type_ = appbox::ProjectType::Standalone;

    Toolbar*          toolbar_ = nullptr;
    SideNav*          side_nav_ = nullptr;
    wxSimplebook*     workspace_ = nullptr;
    FilesystemPanel*  filesystem_panel_ = nullptr;
    RegistryPanel*    registry_panel_ = nullptr;
    NetworkPanel*     network_panel_ = nullptr;
    EnvironmentPanel* environment_panel_ = nullptr;
    DebugPanel*       debug_panel_ = nullptr;
    SettingsPanel*    settings_panel_ = nullptr;

    /**
     * @brief Snapshot file which holds the baseline of the session.
     *
     * The file is the baseline: `Capture and Diff` writes the state of the
     * machine after the change into its phase after the change and compares
     * the two phases of the file. An empty path means that the session holds
     * no baseline, which is what the commands report when a comparison is
     * asked for before a state was captured.
     *
     * The path belongs to the session alone: it names a file of the machine
     * the packer runs on and not a setting of the configuration, so it never
     * travels with the project file.
     */
    std::wstring snapshot_path_;

    /**
     * @brief Snapshot file the running capture works with.
     *
     * The path is kept while the run is in flight, because the result of the
     * run decides what the session does with it: a baseline which was read as
     * a whole becomes the baseline of the session, while a comparison leaves
     * the session without one.
     */
    std::wstring capture_path_;

    bool output_path_edited_ = false;

    std::thread       pack_thread_;
    ActionDialog*     progress_dialog_ = nullptr;
    std::atomic<bool> pack_cancelled_{ false };
    bool              run_after_pack_ = false;

    /**
     * @brief Number of files the packing stage reports.
     *
     * The extracting stage of a `Build and Run` run continues the progress bar
     * where the packing stage ended, so its values are offset by this count.
     */
    std::size_t pack_phase_total_ = 0;

    /**
     * @brief Maximum of the progress bar, which is also the value of its
     *        finished state.
     */
    int pack_range_ = 0;

    /**
     * @brief Last report of the running stage.
     *
     * The action dialog reads it whenever a report of the worker thread
     * arrives.
     */
    appbox::BuildProgress last_progress_;

    /**
     * @brief Whether the run finished and the dialog presents its result.
     */
    bool pack_finished_ = false;

    std::thread       capture_thread_;
    ActionDialog*     capture_dialog_ = nullptr;
    std::atomic<bool> capture_cancelled_{ false };

    /**
     * @brief Whether the capture finished and its dialog was dismissed.
     */
    bool capture_finished_ = false;

    /**
     * @brief Last progress report of the running capture.
     */
    appbox::CaptureProgress last_capture_progress_;
};

#endif // APPBOX_PACKER_WIDGET_MAIN_FRAME_HPP
