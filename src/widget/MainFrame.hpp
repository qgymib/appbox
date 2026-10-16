#ifndef APPBOX_PACKER_WIDGET_MAIN_FRAME_HPP
#define APPBOX_PACKER_WIDGET_MAIN_FRAME_HPP

#include <wx/wx.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "core/ApplicationMetadata.hpp"
#include "core/BuildReport.hpp"
#include "core/CaptureWizardModel.hpp"
#include "core/EnvironmentModel.hpp"
#include "core/FilesystemIsolationModel.hpp"
#include "core/NetworkModel.hpp"
#include "core/PackModel.hpp"
#include "core/ProjectType.hpp"
#include "core/RealtimeCapture.hpp"
#include "core/RegistryModel.hpp"
#include "core/SnapshotApply.hpp"
#include "core/SnapshotCapture.hpp"
#include "core/SnapshotStore.hpp"
#include "core/StartupScreen.hpp"

class ActionDialog;
class CaptureWizard;
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
wxDECLARE_EVENT(APPBOX_REALTIME_CAPTURE_PROGRESS, wxThreadEvent);
wxDECLARE_EVENT(APPBOX_REALTIME_CAPTURE_STAGE, wxThreadEvent);
wxDECLARE_EVENT(APPBOX_REALTIME_CAPTURE_FINISHED, wxThreadEvent);
wxDECLARE_EVENT(APPBOX_CAPTURE_FILL_PROGRESS, wxThreadEvent);
wxDECLARE_EVENT(APPBOX_CAPTURE_FILL_FINISHED, wxThreadEvent);

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
 * @brief Result of one phase of a snapshot capture.
 *
 * The phase which reads the baseline writes the state of the machine into a new
 * snapshot file and reports the counts of that file; the phase which compares
 * reads the state again, writes it into the same file and compares the two
 * states. Nothing is written into the workspaces here: the difference travels
 * with the result and the window fills it into the workspaces on the last step
 * of the wizard.
 */
struct SnapshotCaptureOutcome
{
    /**
     * @brief Error description, empty when the run succeeded.
     */
    std::string error;

    /**
     * @brief Number of the entries the run wrote into the phase, all zero for
     *        a run which failed or which compared.
     */
    appbox::SnapshotPhaseCounts counts;

    /**
     * @brief The difference of the two states, null when the run failed.
     */
    std::shared_ptr<const appbox::SnapshotDiff> diff;
};

/**
 * @brief Result of one realtime capture recording.
 *
 * The recording itself changes nothing: the analysis of the recorded events
 * produces the difference of the machine, which the window fills into the
 * workspaces on the last step of the wizard. The changes the recording saw
 * which belong to no preset directory travel with the result as well, so the
 * dialog lists them next to the entries the fill leaves out.
 */
struct RealtimeCaptureOutcome
{
    /**
     * @brief Error description, empty when the recording was analyzed.
     */
    std::string error;

    /**
     * @brief The difference of the recording, null when the run failed.
     */
    std::shared_ptr<const appbox::SnapshotDiff> diff;

    /**
     * @brief The changes of the recording which the workspaces cannot carry.
     */
    appbox::CaptureFillReport report;
};

/**
 * @brief Result of the fill which writes a difference into the workspaces.
 *
 * The fill runs on a thread of its own, because it reads the host for every
 * entry it writes and because the dialog reports the entry it works on while it
 * runs. The models travel with the result, so the window which owns them swaps
 * them in when the run is over instead of letting the thread write the state
 * the workspaces show.
 */
struct CaptureFillOutcome
{
    /**
     * @brief Error description, empty when the difference was written.
     */
    std::string error;

    /**
     * @brief What the fill did and what it left out.
     */
    appbox::CaptureFillReport report;

    /**
     * @brief Whether the models below hold the state the fill produced.
     */
    bool applied = false;

    appbox::PackModel        model;
    appbox::RegistryModel    registry;
    appbox::EnvironmentModel environment;
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
     * @brief The phase of a snapshot capture which runs on the capture thread.
     */
    enum class CapturePhase
    {
        /**
         * @brief The state of the machine is read into a new snapshot file,
         *        which is the baseline of the comparison.
         */
        Baseline,

        /**
         * @brief The state of the machine is read again, compared with the
         *        baseline and turned into the plan of the difference.
         */
        Comparison,
    };

    /**
     * @brief Ask for the file the baseline of a snapshot capture is kept in.
     *
     * The call is what the wizard asks for when the user presses the button
     * which browses for the file: a new source asks for a file which does not
     * have to exist, an existing one for a file an earlier run wrote. The
     * dialog only names the file, the state of the machine is not read here.
     *
     * @param[in] source Source of the baseline the user picked.
     * @param[out] path Path of the file the user picked, unchanged when the
     *                  dialog was dismissed.
     * @return true when the user picked a file.
     */
    bool OnSnapshotPathBrowse(appbox::SnapshotBaselineSource source, std::wstring& path);

    /**
     * @brief Read the baseline of a snapshot capture.
     *
     * The call is what the wizard asks for when it enters the step of the
     * baseline: the path is checked first, and a new source then reads the
     * state of the machine into that file while an existing one opens the file
     * and uses the state before the change it holds. The wizard is told how the
     * request ended through CaptureWizard::SetBaselineReady(), whatever the
     * answer is, so the step which names the file is shown again when the run
     * produced no baseline.
     *
     * @param[in] source Source of the baseline the user picked.
     * @param[in] path Path of the file the baseline is kept in.
     */
    void OnSnapshotBaselineRequested(appbox::SnapshotBaselineSource source, const std::wstring& path);

    /**
     * @brief Read the state of the machine again and compare it with the
     *        baseline.
     *
     * The call is what the wizard asks for when it enters the step of the
     * comparison, and it is repeated when the user asks for it again after a
     * run which failed.
     */
    void StartSnapshotComparison();

    /**
     * @brief Start the background run of one phase of a snapshot capture.
     *
     * The run reports its progress into the wizard and its result through
     * APPBOX_CAPTURE_FINISHED. The state is written while it is read, so a
     * capture of a whole machine never travels through the memory of the
     * packer.
     *
     * @param[in] phase The phase to run.
     * @param[in] path Path of the snapshot file the run works with: the file
     *                 which is written as the baseline, or the file which
     *                 holds the baseline and receives the state after the
     *                 change.
     */
    void StartSnapshotCapture(CapturePhase phase, const std::wstring& path);

    /**
     * @brief Forward capture progress into the wizard.
     * @param[in] event Thread event carrying the progress report.
     */
    void OnCaptureProgress(wxThreadEvent& event);

    /**
     * @brief Show the last capture report in the wizard.
     *
     * The wizard keeps the elapsed time and the cycle of its bar on its own
     * clock, so a report only updates the action, the object and the entry
     * counter of the step which is running.
     */
    void ApplyCaptureProgress();

    /**
     * @brief Finish a phase of the snapshot capture and present its result.
     *
     * The result of the baseline phase is the file which was written and the
     * counts of the state it holds, which opens the step of the installation;
     * the result of the comparison is the plan of the difference, which the
     * review of the wizard presents. A run which failed leaves the step it
     * belongs to open, so the user can ask for it again.
     *
     * @param[in] event Thread event carrying the capture outcome.
     */
    void OnCaptureFinished(wxThreadEvent& event);

    /**
     * @brief Start a capture of the machine.
     *
     * The command opens the wizard of the capture, which asks for the mode:
     * the realtime capture follows a recording while the user installs a
     * program outside the packer, the snapshot capture reads the state of the
     * machine before and after the installation and compares the two reads.
     * Both modes end in the step which analyzes the changes and writes them into
     * the workspaces of the session, see OnCaptureStageChanged() for the steps
     * of the wizard, OnCaptureFinished() for the phases of the snapshot capture
     * and OnRealtimeCaptureFinished() for the end of a recording.
     *
     * The prerequisites of the recording are shown by the first step of the
     * wizard: a machine which cannot record is told to pick the snapshot
     * capture, and a process which is not elevated is offered the restart as
     * an administrator.
     *
     * @param[in] event Command event.
     */
    void OnCapture(wxCommandEvent& event);

    /**
     * @brief Start the recording of the realtime capture.
     *
     * The call opens the kernel session of the machine on the capture thread:
     * the thread reads the baseline of the persisted environment, starts the
     * recorder and waits for the user to end the recording, after which it
     * stops the session, merges the recorded events into the difference of the
     * machine and builds the plan of that difference.
     */
    void StartRealtimeCaptureRecording();

    /**
     * @brief Follow the wizard of the capture into another step.
     *
     * The wizard tells the window that a step was entered: the recording is
     * started when the user leaves the choice of the mode, the comparison of a
     * snapshot capture is started when the wizard enters its step, and the
     * recording is ended when the user leaves it, which hands the capture
     * thread over to the analysis. The fill of the workspaces is started when
     * the last step is entered with the difference of a snapshot capture
     * already there; the difference of a recording is filled once the analysis
     * of the recording is over, see OnRealtimeCaptureFinished().
     *
     * @param[in] from The step the wizard was on.
     * @param[in] to The step the wizard entered.
     */
    void OnCaptureStageChanged(appbox::CaptureWizardStage from, appbox::CaptureWizardStage to);

    /**
     * @brief Write the difference of the capture into the workspaces.
     *
     * The call is the last phase of both capture modes: the difference is
     * written into copies of the models of the session on a thread of its own,
     * because every entry reads the host and because the dialog reports the
     * entry which is being written while the run goes on. The window swaps the
     * models in once the run is over, which is what keeps a workspace from
     * reading a model another thread writes.
     */
    void StartFill();

    /**
     * @brief Forward the work of the fill into the wizard.
     * @param[in] event Thread event carrying the report of one entry.
     */
    void OnFillProgress(wxThreadEvent& event);

    /**
     * @brief Write the models the fill produced into the session.
     *
     * The run wrote copies of the models, so the call is what makes the change
     * visible: the models are swapped in, the three workspaces are refreshed
     * and the wizard is told that the whole work of the capture is over, which
     * is what enables the button that ends it.
     *
     * @param[in] event Thread event carrying the fill outcome.
     */
    void OnFillFinished(wxThreadEvent& event);

    /**
     * @brief Destroy the wizard of the capture.
     *
     * The wizard is destroyed once its work is over: it is the owner which
     * destroys it and never the wizard itself, so a dialog is never destroyed
     * inside one of its own event handlers.
     */
    void CloseCaptureWizard();

    /**
     * @brief Poll the wizard of the capture while a capture runs.
     *
     * The recording of a quiet machine reports no progress, so the wizard is
     * polled on a timer of its own: a wizard the user dismissed stops the run
     * like the `Cancel` button does, and a wizard whose work is over is
     * destroyed.
     *
     * @param[in] event Timer event.
     */
    void OnCaptureWizardTick(wxTimerEvent& event);

    /**
     * @brief Forward the progress of the recording into the wizard.
     * @param[in] event Thread event carrying the progress report.
     */
    void OnRealtimeCaptureProgress(wxThreadEvent& event);

    /**
     * @brief Show the work the capture thread entered.
     * @param[in] event Thread event carrying the stage code.
     */
    void OnRealtimeCaptureStage(wxThreadEvent& event);

    /**
     * @brief Present the analysis of the recording in the wizard.
     *
     * The result of the analysis is the difference of the machine and the plan
     * of that difference, which the wizard shows on its third step. Nothing is
     * written into the workspaces here: the user confirms the plan first. A
     * cancelled recording closes the wizard and an error is reported to the
     * user, which closes it as well.
     *
     * @param[in] event Thread event carrying the capture outcome.
     */
    void OnRealtimeCaptureFinished(wxThreadEvent& event);

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
     * @brief Snapshot file the running snapshot capture works with.
     *
     * The file is the baseline of the capture: the phase which reads the
     * baseline writes the state of the machine into it and the phase which
     * compares reads the state again into its phase after the change and
     * compares the two phases of the file.
     *
     * The path belongs to the capture alone: it names a file of the machine
     * the packer runs on and not a setting of the configuration, so it never
     * travels with the project file.
     */
    std::wstring capture_snapshot_path_;

    /**
     * @brief The phase of the snapshot capture which is running.
     */
    CapturePhase capture_phase_ = CapturePhase::Baseline;

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
    std::atomic<bool> capture_cancelled_{ false };

    /**
     * @brief Whether a phase of the snapshot capture finished and its result
     *        was presented.
     */
    bool capture_finished_ = false;

    /**
     * @brief Last progress report of the running capture.
     */
    appbox::CaptureProgress last_capture_progress_;

    /**
     * @brief Recorder of the realtime capture of the window.
     *
     * The recorder owns the kernel session of the machine while a capture
     * runs, and it is stopped by its own destructor when the window goes away
     * with a recording in flight.
     */
    appbox::RealtimeCaptureRecorder realtime_recorder_;

    /**
     * @brief Noise tables the running realtime capture works with.
     */
    appbox::RealtimeCaptureOptions realtime_capture_options_;

    std::thread realtime_capture_thread_;

    /**
     * @brief Wizard of the running capture.
     *
     * The wizard serves both capture modes: the mode is picked on its first
     * step and the work of the mode is done by the window, see OnCapture().
     */
    CaptureWizard* capture_wizard_ = nullptr;

    /**
     * @brief Difference of the capture which is filled into the workspaces.
     *
     * The difference is kept from the end of the analysis until the fill of the
     * workspaces is over: it is what the fill writes and what the report names.
     */
    std::shared_ptr<const appbox::SnapshotDiff> capture_diff_;

    /**
     * @brief Timer which polls the wizard while the capture runs.
     */
    wxTimer* capture_wizard_timer_ = nullptr;

    /**
     * @brief Thread which writes the difference of the capture into the models.
     */
    std::thread fill_thread_;

    /**
     * @brief The changes the capture cannot write.
     *
     * The report is the one channel of the last step of the wizard: the analysis
     * of a recording reports the changes which belong to no preset directory
     * into it and the fill adds the entries it could not write, so the dialog
     * lists both of them.
     */
    appbox::CaptureFillReport capture_report_;

    std::atomic<bool> realtime_capture_cancelled_{ false };

    /**
     * @brief Whether the user ended the recording and the capture thread is
     *        analyzing it.
     */
    std::atomic<bool> realtime_capture_finishing_{ false };

    /**
     * @brief Whether the realtime capture finished and the wizard presents
     *        the analysis.
     */
    bool realtime_capture_finished_ = false;

    /**
     * @brief Last progress report of the running realtime capture.
     */
    appbox::RealtimeCaptureProgress last_realtime_progress_;
};

#endif // APPBOX_PACKER_WIDGET_MAIN_FRAME_HPP
