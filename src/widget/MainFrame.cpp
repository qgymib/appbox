#include "MainFrame.hpp"
#include "FilesystemPanel.hpp"
#include "MainProgramDialog.hpp"
#include "PlaceholderPanel.hpp"
#include "RibbonBar.hpp"
#include "SideNav.hpp"
#include "LoaderResource.hpp"
#include "core/BuildReport.hpp"
#include "core/PackService.hpp"
#include "core/PresetDirectory.hpp"
#include "core/ProjectFile.hpp"
#include "core/ZipReader.hpp"
#include "WString.hpp"
#include <wx/artprov.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/icon.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/simplebook.h>
#include <wx/strconv.h>
#include <wx/timer.h>
#include <wx/utils.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

wxDEFINE_EVENT(APPBOX_PACK_PROGRESS, wxThreadEvent);
wxDEFINE_EVENT(APPBOX_PACK_FINISHED, wxThreadEvent);

namespace
{

/**
 * @brief Build a unique folder name for the extracted archive.
 * @return The folder name.
 */
std::wstring UniqueExtractFolder()
{
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return L"AppBox-" + std::to_wstring(ticks);
}

/**
 * @brief Name of the icon resource embedded by resource.rc.
 *
 * The resource compiler stores resource names in upper case and the lookup is
 * case insensitive, so the name is spelled the way it appears in the resource
 * directory of the executable.
 */
constexpr const char* kWindowIconResource = "IDI_ICON1";

/**
 * @brief Filter of the project file dialogs.
 *
 * A project file is JSON text encoded as UTF-8; the filter keeps the
 * selection on that format without forbidding other names.
 */
constexpr const char* kProjectFileFilter = "JSON configuration (*.json)|*.json";

/**
 * @brief Command identifiers of the configuration commands.
 *
 * The commands own identifiers instead of reusing a standard one such as
 * wxID_OPEN or wxID_SAVE, so they never collide with a built in handler.
 */
const int kMenuImportConfiguration = wxNewId();
const int kMenuExportConfiguration = wxNewId();

/**
 * @brief Identifier and interval of the timer which refreshes the elapsed time.
 *
 * The timer only updates the text of the progress dialog while the run is
 * going on; a report of the worker thread does the same with a new file name.
 */
const int kProgressTimerId = wxNewId();
const int kProgressTimerInterval = 500;

/**
 * @brief Measure the room a progress message needs inside the dialog.
 *
 * The native task dialog keeps the size it was created with, so the measured
 * room tells the dialog when a message is too big for it, see
 * MainFrame::UpdateProgressDialog().
 *
 * @param[in] text Message text.
 * @return Room the message needs.
 */
appbox::MessageExtent MeasureProgressText(const wxString& text)
{
    return appbox::MeasureMessageExtent(text.ToStdString(wxConvUTF8));
}

} // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "AppBox", wxDefaultPosition, wxSize(1180, 720))
{
    SetMinSize(wxSize(720, 480));

    ApplyWindowIcon();

    CreateMenuBar();
    CreateLayout();

    CreateStatusBar(1);
    UpdateStatusBar();

    ribbon_->SetOutputPath(DefaultOutputPath());
    UpdateTitle();

    Bind(wxEVT_MENU, &MainFrame::OnExit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
    Bind(wxEVT_MENU, &MainFrame::OnImportConfiguration, this, kMenuImportConfiguration);
    Bind(wxEVT_MENU, &MainFrame::OnExportConfiguration, this, kMenuExportConfiguration);
    Bind(APPBOX_SIDE_NAV, &MainFrame::OnSideNavChanged, this);
    Bind(wxEVT_RIBBONBUTTONBAR_CLICKED, &MainFrame::OnSelectMainProgram, this, kRibbonStartupFiles);
    Bind(wxEVT_RIBBONBUTTONBAR_CLICKED, &MainFrame::OnBuild, this, kRibbonBuild);
    Bind(wxEVT_RIBBONBUTTONBAR_CLICKED, &MainFrame::OnBuildAndRun, this, kRibbonBuildAndRun);
    Bind(wxEVT_BUTTON, &MainFrame::OnBrowseOutput, this, kRibbonBrowseOutput);
    Bind(wxEVT_TEXT, &MainFrame::OnOutputPathEdited, this, kRibbonOutputPath);
    Bind(APPBOX_PACK_PROGRESS, &MainFrame::OnPackProgress, this);
    Bind(APPBOX_PACK_FINISHED, &MainFrame::OnPackFinished, this);
}

MainFrame::~MainFrame()
{
    if (pack_thread_.joinable())
    {
        pack_cancelled_ = true;
        pack_thread_.join();
    }

    if (progress_timer_ != nullptr)
    {
        progress_timer_->Stop();
        delete progress_timer_;
        progress_timer_ = nullptr;
    }

    if (progress_dialog_ != nullptr)
    {
        progress_dialog_->Destroy();
        progress_dialog_ = nullptr;
    }
}

void MainFrame::CreateMenuBar()
{
    auto menu_file = new wxMenu();
    menu_file->Append(kMenuImportConfiguration, "&Import Configuration...");
    menu_file->Append(kMenuExportConfiguration, "&Export Configuration...");
    menu_file->AppendSeparator();
    menu_file->Append(wxID_EXIT);

    auto menu_help = new wxMenu();
    menu_help->Append(wxID_ABOUT);

    auto menu_bar = new wxMenuBar();
    menu_bar->Append(menu_file, "&File");
    menu_bar->Append(menu_help, "&Help");
    SetMenuBar(menu_bar);
}

void MainFrame::CreateLayout()
{
    ribbon_ = new RibbonBar(this, wxID_ANY);

    side_nav_ = new SideNav(this, wxID_ANY);
    side_nav_->AddItem("Filesystem", wxART_FOLDER);
    side_nav_->AddItem("Registry", wxART_HARDDISK);
    side_nav_->AddItem("Network", wxART_GO_FORWARD);
    side_nav_->AddItem("Settings", wxART_HELP);

    workspace_ = new wxSimplebook(this, wxID_ANY);

    filesystem_panel_ = new FilesystemPanel(workspace_, model_);
    workspace_->AddPage(filesystem_panel_, "Filesystem");
    workspace_->AddPage(new PlaceholderPanel(workspace_, "Registry",
                                             "Registry isolation of the packaged application."),
                        "Registry");
    workspace_->AddPage(new PlaceholderPanel(workspace_, "Network",
                                             "Network isolation of the packaged application."),
                        "Network");
    workspace_->AddPage(new PlaceholderPanel(workspace_, "Settings",
                                             "Launch configuration of the packaged application."),
                        "Settings");
    workspace_->SetSelection(static_cast<size_t>(0));

    auto* body = new wxBoxSizer(wxHORIZONTAL);
    body->Add(side_nav_, 0, wxEXPAND);
    body->Add(workspace_, 1, wxEXPAND);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(ribbon_, 0, wxEXPAND);
    sizer->Add(body, 1, wxEXPAND);
    SetSizer(sizer);
}

void MainFrame::ApplyWindowIcon()
{
    /*
     * The icon is loaded from the resource embedded by resource.rc, which is
     * the same image Explorer shows for the executable. SetIcon() also
     * installs the icon of the taskbar button and of the Alt-Tab list.
     */
    const wxIcon icon(kWindowIconResource, wxBITMAP_TYPE_ICO_RESOURCE);
    if (icon.IsOk())
    {
        SetIcon(icon);
    }
}

void MainFrame::UpdateStatusBar()
{
    if (!model_.HasMainProgram())
    {
        SetStatusText("No main program selected");
        return;
    }

    appbox::PresetDirectory preset;
    if (!appbox::FindPresetDirectory(model_.MainProgramChoice().preset_id, preset))
    {
        SetStatusText("No main program selected");
        return;
    }

    const auto& program = model_.MainProgramChoice();
    SetStatusText(wxString::Format("Main program: %s / %s", preset.display_name,
                                   program.import_name + "\\" + program.relative_path));
}

void MainFrame::UpdateTitle()
{
    SetTitle("AppBox [" + OutputPath() + "]");
}

wxString MainFrame::OutputPath() const
{
    if (ribbon_ != nullptr)
    {
        const auto edited = ribbon_->GetOutputPath();
        if (!edited.empty())
        {
            return edited;
        }
    }
    return DefaultOutputPath();
}

wxString MainFrame::DefaultOutputPath() const
{
    wxFileName derived(wxFileName::GetCwd(), "AppBoxPackage.zip");
    if (model_.HasMainProgram())
    {
        derived.SetName(model_.MainProgramChoice().import_name);
        derived.SetExt("zip");
    }
    return derived.GetFullPath();
}

void MainFrame::OnOutputPathEdited(wxCommandEvent& event)
{
    output_path_edited_ = true;
    UpdateTitle();
    event.Skip();
}

void MainFrame::OnSideNavChanged(wxCommandEvent& event)
{
    const auto index = event.GetInt();
    if (index >= 0 && static_cast<size_t>(index) < workspace_->GetPageCount())
    {
        workspace_->SetSelection(static_cast<size_t>(index));
    }
    event.Skip();
}

void MainFrame::OnExit(wxCommandEvent&)
{
    Close(true);
}

void MainFrame::OnAbout(wxCommandEvent&)
{
    wxMessageBox("AppBox packages an installed application into a portable zip "
                 "archive: the imported folders become lower filesystem layers and the "
                 "embedded loader starts the sandboxed application.\n\n"
                 "The loader and its configuration carry the file name of the startup "
                 "file, so extracting the archive and running that program requires no "
                 "further installation.",
                 "About AppBox", wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnImportConfiguration(wxCommandEvent&)
{
    wxFileDialog dialog(this, "Import Configuration", wxEmptyString, wxEmptyString,
                        kProjectFileFilter, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    /*
     * Importing replaces the whole configuration, so an existing one is only
     * dropped after the user confirmed it.
     */
    if (!model_.IsEmpty())
    {
        const auto answer =
            wxMessageBox("Importing a configuration replaces the imported folders, the imported "
                         "files and the main program selection of the current session.\n\n"
                         "Continue?",
                         "Import Configuration", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this);
        if (answer != wxYES)
        {
            return;
        }
    }

    appbox::PackModel loaded;
    std::wstring      output_path;
    std::string       error;

    if (!appbox::LoadProject(dialog.GetPath().ToStdWstring(), loaded, output_path, error))
    {
        spdlog::error("importing the configuration failed: {}", error);
        wxMessageBox("The configuration could not be imported:\n\n" + wxString::FromUTF8(error),
                     "Import Configuration", wxOK | wxICON_ERROR, this);
        return;
    }

    model_ = std::move(loaded);
    filesystem_panel_->RefreshModel();

    /*
     * A path recorded by the project file becomes the authoritative archive
     * path; without one the path keeps following the main program.
     */
    if (!output_path.empty())
    {
        output_path_edited_ = true;
        ribbon_->SetOutputPath(wxString(output_path));
    }
    else
    {
        output_path_edited_ = false;
        ribbon_->SetOutputPath(DefaultOutputPath());
    }

    UpdateStatusBar();
    UpdateTitle();
    SetStatusText("Configuration imported from " + dialog.GetPath());
}

void MainFrame::OnExportConfiguration(wxCommandEvent&)
{
    /*
     * The archive path of the current configuration names the project, so the
     * dialog suggests a project file next to the archive.
     */
    wxFileName suggested(OutputPath());
    suggested.SetExt("json");

    wxFileDialog dialog(this, "Export Configuration", suggested.GetPath(), suggested.GetFullName(),
                        kProjectFileFilter, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    std::string error;
    if (!appbox::SaveProject(model_, OutputPath().ToStdWstring(),
                             dialog.GetPath().ToStdWstring(), error))
    {
        spdlog::error("exporting the configuration failed: {}", error);
        wxMessageBox("The configuration could not be exported:\n\n" + wxString::FromUTF8(error),
                     "Export Configuration", wxOK | wxICON_ERROR, this);
        return;
    }

    SetStatusText("Configuration exported to " + dialog.GetPath());
}

void MainFrame::OnSelectMainProgram(wxCommandEvent&)
{
    MainProgramDialog dialog(this, model_);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    const auto& selection = dialog.Selection();
    std::string error;
    if (!model_.SetMainProgram(selection.preset_id, selection.import_name, selection.relative_path,
                               error))
    {
        wxMessageBox(error, "Select Main Program", wxOK | wxICON_ERROR, this);
        return;
    }

    filesystem_panel_->RefreshModel();
    UpdateStatusBar();

    /* Keep following the main program until the user edits the path. */
    if (!output_path_edited_)
    {
        ribbon_->SetOutputPath(DefaultOutputPath());
    }
    UpdateTitle();
}

void MainFrame::OnBrowseOutput(wxCommandEvent&)
{
    wxFileName current(OutputPath());

    wxFileDialog dialog(this, "Output Archive", wxEmptyString, current.GetFullName(),
                        "Zip archive (*.zip)|*.zip", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    output_path_edited_ = true;
    ribbon_->SetOutputPath(dialog.GetPath());
    UpdateTitle();
}

void MainFrame::OnBuild(wxCommandEvent&)
{
    StartPack(false);
}

void MainFrame::OnBuildAndRun(wxCommandEvent&)
{
    StartPack(true);
}

void MainFrame::StartPack(bool run_after)
{
    if (pack_thread_.joinable())
    {
        return;
    }

    if (!model_.HasMainProgram())
    {
        wxMessageBox("Select the main program before building.", "Build",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }

    const auto zip_path = OutputPath();
    if (zip_path.empty())
    {
        wxMessageBox("Choose the output archive path first.", "Build",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }

    std::string error;
    const auto loader = appbox::LoadEmbeddedLoader(error);
    if (!error.empty() || loader.empty())
    {
        wxMessageBox("The embedded loader is unavailable: " + error, "Build", wxOK | wxICON_ERROR,
                     this);
        return;
    }

    /*
     * Count the files once so the progress callback has a stable total. The
     * loader payload and its configuration are archive entries of their own,
     * so the packing stage reports this count and the extracting stage of a
     * `Build and Run` run reports the very same number of file entries.
     */
    std::size_t total = 2 + model_.AllImportedFiles().size();
    for (const auto& entry : appbox::PresetDirectories())
    {
        for (const auto& imported : model_.ImportsOf(entry.id))
        {
            total += appbox::CountFilesBelow(imported.source_path);
        }
    }
    const auto planned = run_after ? total * 2 : total;
    const auto range =
        static_cast<int>(std::min<std::size_t>(planned, std::numeric_limits<int>::max()));

    /*
     * The dialog reports the packing progress and turns into the result
     * display of the same dialog once the maximum is reached: without
     * wxPD_AUTO_HIDE it stays visible, keeps its progress bar and its Cancel
     * button becomes Close.
     *
     * The timing flags of wxWidgets are left out on purpose: they make the
     * native dialog carry a collapsible details area which the user has to
     * open to see the elapsed time. The time is part of the message instead.
     */
    pack_start_ = std::chrono::steady_clock::now();
    last_progress_ = appbox::BuildProgress{appbox::BuildStage::Preparing, 0, total, {}};

    const auto initial_text = wxString::FromUTF8(
        appbox::BuildProgressMessage(last_progress_, std::chrono::milliseconds::zero()));

    progress_dialog_ = new wxProgressDialog(run_after ? "Building and Running" : "Building",
                                            initial_text, range, this,
                                            wxPD_APP_MODAL | wxPD_CAN_ABORT);

    /*
     * The dialog starts with the size of this message, so the room it needed
     * is the baseline for the re-fits done by UpdateProgressDialog().
     */
    const auto initial_extent = MeasureProgressText(initial_text);
    progress_fitted_lines_ = initial_extent.lines;
    progress_fitted_width_ = initial_extent.width;

    pack_cancelled_ = false;
    pack_finished_ = false;
    pack_range_ = range;
    pack_phase_total_ = total;
    run_after_pack_ = run_after;

    /*
     * A single large file produces no report while it is packed, so the time
     * shown by the dialog is refreshed by a timer as well.
     */
    if (progress_timer_ == nullptr)
    {
        progress_timer_ = new wxTimer(this, kProgressTimerId);
        Bind(wxEVT_TIMER, &MainFrame::OnProgressTimer, this, kProgressTimerId);
    }
    progress_timer_->Start(kProgressTimerInterval);

    /*
     * The model is copied so a later UI action cannot mutate the archive
     * content while the worker reads it.
     */
    const auto snapshot = model_;
    const auto loader_bytes = std::string(loader);
    const auto zip_wide = zip_path.ToStdWstring();

    pack_thread_ = std::thread([this, snapshot, loader_bytes, zip_wide, run_after]() {
        const auto report_progress = [this](const appbox::BuildProgress& report) {
            auto* event = new wxThreadEvent(APPBOX_PACK_PROGRESS);
            event->SetPayload(report);
            this->GetEventHandler()->QueueEvent(event);
            return !this->pack_cancelled_.load();
        };

        PackOutcome outcome;
        outcome.error =
            appbox::Pack(snapshot, loader_bytes.data(), loader_bytes.size(), zip_wide, report_progress);

        if (outcome.error.empty())
        {
            /* The loader is named after the main program of the snapshot. */
            outcome.loader_entry = appbox::LoaderEntryName(snapshot);
            outcome.archive_path = zip_wide;
        }

        if (outcome.error.empty() && run_after)
        {
            const auto folder = std::filesystem::temp_directory_path() / UniqueExtractFolder();
            outcome.error = appbox::ExtractArchive(zip_wide, folder.wstring(), report_progress);
            if (outcome.error.empty())
            {
                outcome.extract_dir = folder.wstring();
            }
        }

        auto* event = new wxThreadEvent(APPBOX_PACK_FINISHED);
        event->SetPayload(outcome);
        this->GetEventHandler()->QueueEvent(event);
    });
}

void MainFrame::UpdateProgressDialog(int value, const wxString& text)
{
    /*
     * The native task dialog keeps the size it was created with, so a message
     * which is longer than the ones shown before would push the buttons at the
     * bottom of the dialog out of its visible area. Re-fitting the dialog only
     * when the message needs more room than every message before it keeps the
     * size stable while the run is going on.
     */
    const auto extent = MeasureProgressText(text);
    if (extent.lines > progress_fitted_lines_ || extent.width > progress_fitted_width_)
    {
        progress_fitted_lines_ = extent.lines;
        progress_fitted_width_ = extent.width;
        progress_dialog_->Fit();
    }

    if (!progress_dialog_->Update(value, text))
    {
        pack_cancelled_ = true;
    }
}

void MainFrame::RefreshProgressDialog()
{
    if (progress_dialog_ == nullptr || pack_finished_)
    {
        return;
    }

    /*
     * The extracting stage continues the progress bar where the packing stage
     * ended, so both stages advance a single bar.
     */
    const auto handled = last_progress_.stage == appbox::BuildStage::Extracting
                             ? pack_phase_total_ + last_progress_.done
                             : last_progress_.done;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - pack_start_);

    const auto text = wxString::FromUTF8(appbox::BuildProgressMessage(last_progress_, elapsed));
    UpdateProgressDialog(appbox::BuildProgressValue(handled, pack_range_), text);
}

void MainFrame::OnPackProgress(wxThreadEvent& event)
{
    if (progress_dialog_ == nullptr || pack_finished_)
    {
        return;
    }

    last_progress_ = event.GetPayload<appbox::BuildProgress>();
    RefreshProgressDialog();
}

void MainFrame::OnProgressTimer(wxTimerEvent&)
{
    RefreshProgressDialog();
}

void MainFrame::OnPackFinished(wxThreadEvent& event)
{
    if (pack_thread_.joinable())
    {
        pack_thread_.join();
    }

    /* A late progress report must not overwrite the result text below. */
    pack_finished_ = true;

    /*
     * The timer must not touch the dialog anymore: reaching the maximum turns
     * the dialog into its modal completion state, which waits for the user, so
     * a later update would replace the result text.
     */
    if (progress_timer_ != nullptr)
    {
        progress_timer_->Stop();
    }

    const auto outcome = event.GetPayload<PackOutcome>();
    const auto run_after = run_after_pack_;
    run_after_pack_ = false;

    auto result = appbox::BuildOutcome::ArchiveWritten;
    std::string result_error;
    wxString final_status;

    if (outcome.error == appbox::kBuildCancelledError)
    {
        result = appbox::BuildOutcome::Cancelled;
    }
    else if (!outcome.error.empty())
    {
        result = appbox::BuildOutcome::Failed;
        result_error = outcome.error;
    }
    else if (run_after && !outcome.extract_dir.empty())
    {
        /*
         * The loader carries the file name of the main program, so it is
         * started through the name the pack run resolved.
         */
        long pid = 0;
        if (!outcome.loader_entry.empty())
        {
            const auto loader = std::filesystem::path(outcome.extract_dir) / outcome.loader_entry;
            pid = wxExecute("\"" + wxString(loader.wstring()) + "\"", wxEXEC_ASYNC);
        }

        if (pid <= 0)
        {
            result = appbox::BuildOutcome::LaunchFailed;
        }
        else
        {
            result = appbox::BuildOutcome::ApplicationStarted;
            final_status = "Running the packaged application from " + wxString(outcome.extract_dir);
        }
    }

    UpdateTitle();
    UpdateStatusBar();
    if (!final_status.empty())
    {
        SetStatusText(final_status);
    }

    if (progress_dialog_ == nullptr)
    {
        return;
    }

    /*
     * The result is presented by the very dialog which reported the packing
     * progress before. A dialog the user cancelled refuses further updates
     * until it is resumed, so the result would be dropped without this.
     */
    if (progress_dialog_->WasCancelled())
    {
        progress_dialog_->Resume();
    }

    /*
     * Reaching the maximum turns the dialog into its modal completion state:
     * it keeps the progress bar, renames Cancel to Close and does not return
     * before the user dismissed it. The result names the archive which was
     * written, so the path does not have to be looked up in the output box.
     *
     * The result is also the message with the most lines of the whole run, so
     * it re-fits the dialog before it is shown: without this the buttons at
     * the bottom would be outside of the visible area.
     */
    UpdateProgressDialog(
        pack_range_,
        wxString::FromUTF8(appbox::BuildResultMessage(result, outcome.archive_path, result_error)));

    progress_dialog_->Destroy();
    progress_dialog_ = nullptr;
}
