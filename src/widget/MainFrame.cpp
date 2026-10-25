#include "MainFrame.hpp"
#include "AboutDialog.hpp"
#include "ActionDialog.hpp"
#include "CaptureWizard.hpp"
#include "EnvironmentPanel.hpp"
#include "FilesystemPanel.hpp"
#include "StartupFilesDialog.hpp"
#include "NetworkPanel.hpp"
#include "RegistryPanel.hpp"
#include "SettingsPanel.hpp"
#include "SideNav.hpp"
#include "Toolbar.hpp"
#include "DebugPanel.hpp"
#include "core/ActionThrottle.hpp"
#include "core/BuildReport.hpp"
#include "EmbeddedResource.hpp"
#include "core/EmbeddedResourceIds.h"
#include "core/PackService.hpp"
#include "core/PresetDirectory.hpp"
#include "core/ProjectFile.hpp"
#include "core/RegFile.hpp"
#include "ZipReader.hpp"
#include "WString.hpp"
#include <wx/artprov.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/icon.h>
#include <wx/msgdlg.h>
#include <wx/simplebook.h>
#include <wx/strconv.h>
#include <wx/utils.h>
#include <windows.h>
#include <shellapi.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

wxDEFINE_EVENT(APPBOX_PACK_PROGRESS, wxThreadEvent);
wxDEFINE_EVENT(APPBOX_PACK_FINISHED, wxThreadEvent);
wxDEFINE_EVENT(APPBOX_CAPTURE_PROGRESS, wxThreadEvent);
wxDEFINE_EVENT(APPBOX_CAPTURE_FINISHED, wxThreadEvent);
wxDEFINE_EVENT(APPBOX_REALTIME_CAPTURE_PROGRESS, wxThreadEvent);
wxDEFINE_EVENT(APPBOX_REALTIME_CAPTURE_STAGE, wxThreadEvent);
wxDEFINE_EVENT(APPBOX_REALTIME_CAPTURE_FINISHED, wxThreadEvent);
wxDEFINE_EVENT(APPBOX_CAPTURE_FILL_PROGRESS, wxThreadEvent);
wxDEFINE_EVENT(APPBOX_CAPTURE_FILL_FINISHED, wxThreadEvent);

namespace
{

/**
 * @brief Time between two reports of the fill which reach the dialog.
 *
 * The fill reports every entry twice and a capture of a whole machine walks
 * through thousands of them, so the dialog is updated with the rate a user can
 * read instead of once per entry.
 */
constexpr int kFillReportInterval = 50;

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
const int kMenuImportRegistry = wxNewId();

/**
 * @brief Stage code of the realtime capture which records the machine.
 */
constexpr int kRealtimeStageRecording = 0;

/**
 * @brief Stage code of the realtime capture which analyzes the recording.
 */
constexpr int kRealtimeStageAnalyzing = 1;

/**
 * @brief Index of the files field of the build dialog.
 */
constexpr std::size_t kBuildFieldFiles = 0;

/**
 * @brief Index of the status field of the build dialog.
 */
constexpr std::size_t kBuildFieldStatus = 1;

/**
 * @brief Label the action row of the capture dialog from the stage of the run.
 *
 * @param[in] stage Stage the capture works on.
 * @return Text of the action row.
 */
wxString CaptureStageAction(appbox::CaptureStage stage)
{
    switch (stage)
    {
    case appbox::CaptureStage::Filesystem:
        return "Scanning the filesystem";
    case appbox::CaptureStage::Registry:
        return "Scanning the registry";
    case appbox::CaptureStage::Environment:
        return "Reading the environment";
    }

    return {};
}

/**
 * @brief Build the text of the entries field of the capture dialog.
 *
 * The count of the entries which could not be read follows the count of the
 * entries which were recorded, but only once the capture skipped something.
 *
 * @param[in] progress Progress report of the capture.
 * @return Text of the entries field.
 */
wxString CaptureEntriesField(const appbox::CaptureProgress& progress)
{
    auto text = wxString::Format("%llu entries", static_cast<unsigned long long>(progress.done));
    if (progress.skipped > 0)
    {
        text += wxString::Format(", %llu skipped", static_cast<unsigned long long>(progress.skipped));
    }
    return text;
}

/**
 * @brief Label the action row of the build dialog from the stage of the run.
 *
 * @param[in] stage Stage the run works on.
 * @return Text of the action row.
 */
wxString BuildStageAction(appbox::BuildStage stage)
{
    switch (stage)
    {
    case appbox::BuildStage::Preparing:
        return "Preparing the archive";
    case appbox::BuildStage::Packing:
        return "Packing files";
    case appbox::BuildStage::Extracting:
        return "Extracting files";
    }

    return {};
}

/**
 * @brief Build the text of the files field of the build dialog.
 *
 * The total is not part of the text while the stage does not know it, so the
 * field counts the handled files only instead of showing a total of zero.
 *
 * @param[in] progress Progress report of the running stage.
 * @return Text of the files field.
 */
wxString BuildFilesField(const appbox::BuildProgress& progress)
{
    const auto done = static_cast<unsigned long long>(progress.done);
    if (progress.total == 0)
    {
        return wxString::Format("%llu", done);
    }
    return wxString::Format("%llu / %llu", done, static_cast<unsigned long long>(progress.total));
}

/**
 * @brief Label the action row of the build dialog from the final state of the
 *        run.
 *
 * @param[in] outcome Final state of the build run.
 * @return Text of the action row.
 */
wxString BuildResultAction(appbox::BuildOutcome outcome)
{
    switch (outcome)
    {
    case appbox::BuildOutcome::ArchiveWritten:
        return "The archive was written successfully.";
    case appbox::BuildOutcome::ApplicationStarted:
        return "The archive was written successfully.";
    case appbox::BuildOutcome::Cancelled:
        return "The build run was cancelled; no archive was written.";
    case appbox::BuildOutcome::Failed:
        return "The build run failed.";
    case appbox::BuildOutcome::LaunchFailed:
        return "The archive was written, but the packaged launcher could not be started.";
    }

    return {};
}

/**
 * @brief Build the text of the status field of the build dialog from the final
 *        state of the run.
 *
 * @param[in] outcome Final state of the build run.
 * @return Text of the status field.
 */
wxString BuildResultStatus(appbox::BuildOutcome outcome)
{
    switch (outcome)
    {
    case appbox::BuildOutcome::ArchiveWritten:
        return "Done";
    case appbox::BuildOutcome::ApplicationStarted:
        return "Application started";
    case appbox::BuildOutcome::Cancelled:
        return "Cancelled";
    case appbox::BuildOutcome::Failed:
        return "Failed";
    case appbox::BuildOutcome::LaunchFailed:
        return "Launch failed";
    }

    return {};
}

/**
 * @brief Build the filter of the snapshot file dialogs.
 *
 * The extension is derived from the constant of the snapshot store, so the
 * dialog offers exactly the files the store writes.
 *
 * @return The filter of the file dialog.
 */
wxString SnapshotFileFilter()
{
    const auto pattern = wxString("*.") + appbox::kSnapshotFileExtension;
    return "AppBox snapshots (" + pattern + ")|" + pattern + "|All files (*.*)|*.*";
}

/**
 * @brief Build the filter of the startup image dialog.
 *
 * The filter is derived from the formats the launcher decodes, so the dialog
 * offers exactly the files a session can use and the two cannot drift apart.
 *
 * @return The filter of the file dialog.
 */
wxString StartupImageFilter()
{
    wxString patterns;
    for (const auto& extension : appbox::StartupImageExtensions())
    {
        if (!patterns.empty())
        {
            patterns += ";";
        }
        patterns += "*" + wxString(extension);
    }

    return "Images (" + patterns + ")|" + patterns + "|All files (*.*)|*.*";
}

/**
 * @brief Read the state of the machine into one phase of a snapshot file.
 *
 * The state is written while it is read, so a capture of a whole machine never
 * travels through the memory of the packer. A run which reports a failure
 * leaves the file usable: the phase carries no marker, so no comparison reads
 * its rows, and a baseline which was created for this run alone is removed
 * again.
 *
 * @param[in] options The roots the capture walks through.
 * @param[in] path Path of the snapshot file.
 * @param[in] diff Whether the file holds the baseline and receives the state
 *                 after the change; a run without it creates the file.
 * @param[in] progress Progress callback of the run.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool WriteCapturePhase(const appbox::CaptureOptions& options, const std::wstring& path, bool diff,
                       const appbox::CaptureProgressCallback& progress, std::string& error)
{
    const auto phase = diff ? appbox::SnapshotPhase::After : appbox::SnapshotPhase::Before;

    /* A run which compares opens the file of the baseline, a run without it creates one. */
    const auto writer = diff ? appbox::SnapshotWriter::Open(path, error) : appbox::SnapshotWriter::Create(path, error);
    if (writer == nullptr)
    {
        return false;
    }

    if (!writer->BeginPhase(phase, error))
    {
        if (!diff)
        {
            writer->DiscardFile();
        }
        return false;
    }

    if (!appbox::CaptureSnapshot(options, *writer, progress, error))
    {
        writer->AbandonPhase();
        if (!diff)
        {
            writer->DiscardFile();
        }
        return false;
    }

    if (!writer->CommitPhase(error))
    {
        writer->AbandonPhase();
        if (!diff)
        {
            writer->DiscardFile();
        }
        return false;
    }

    return true;
}

/**
 * @brief Whether a snapshot file holds the state a comparison can read.
 *
 * Only the state before the change is checked: the comparison writes the state
 * after the change into the same file, so a file which an earlier run left with
 * a state after the change is usable as well.
 *
 * @param[in] path Path of the snapshot file.
 * @param[out] counts Counts of the state before the change.
 * @param[out] error Error description when the file is not usable.
 * @return true when the file holds the state.
 */
bool ReadSnapshotBaseline(const std::wstring& path, appbox::SnapshotPhaseCounts& counts, std::string& error)
{
    const auto reader = appbox::SnapshotReader::Open(path, error);
    if (reader == nullptr)
    {
        return false;
    }

    bool present = false;
    if (!reader->HasPhase(appbox::SnapshotPhase::Before, present, error))
    {
        return false;
    }
    if (!present)
    {
        error = "the snapshot file does not hold the state before the change: " + appbox::WideToUTF8(path);
        return false;
    }

    return reader->Counts(appbox::SnapshotPhase::Before, counts, error);
}

/**
 * @brief Whether the baseline can be written into a file.
 *
 * The path is the one the user typed or picked, so it is checked before the
 * machine is read: a folder which is not there and a file which is not allowed
 * to be replaced would otherwise fail the walk after it did its work.
 *
 * @param[in] path Path of the snapshot file.
 * @param[in] owner Window the questions of the check belong to.
 * @return true when the walk can write the file.
 */
bool CanWriteSnapshotFile(const std::wstring& path, wxWindow* owner)
{
    const std::filesystem::path file(path);
    std::error_code             ec;

    if (file.filename().empty())
    {
        wxMessageBox("The path of the snapshot file names no file.", "Capture", wxOK | wxICON_ERROR, owner);
        return false;
    }

    if (std::filesystem::is_directory(file, ec))
    {
        wxMessageBox("The path of the snapshot file names a folder:\n\n" + wxString(path), "Capture",
                     wxOK | wxICON_ERROR, owner);
        return false;
    }

    const auto folder = file.parent_path();
    if (!folder.empty() && !std::filesystem::is_directory(folder, ec))
    {
        wxMessageBox("The folder the snapshot file is written into does not exist:\n\n" + wxString(folder.wstring()),
                     "Capture", wxOK | wxICON_ERROR, owner);
        return false;
    }

    /*
     * The dialog of the button asks the same question, but the path can be
     * typed: a file which is there is only replaced when the user says so.
     */
    if (std::filesystem::exists(file, ec) && !ec)
    {
        const auto answer = wxMessageBox("The file exists already:\n\n" + wxString(path) +
                                             "\n\nReplace it? The state of the machine is written into it.",
                                         "Capture", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, owner);
        if (answer != wxYES)
        {
            return false;
        }
    }

    return true;
}

} // namespace

MainFrame::MainFrame() : wxFrame(nullptr, wxID_ANY, "AppBox", wxDefaultPosition, wxSize(1180, 720))
{
    SetMinSize(wxSize(720, 480));

    ApplyWindowIcon();

    CreateMenuBar();
    CreateLayout();

    CreateStatusBar(1);
    UpdateStatusBar();

    settings_panel_->SetOutputPath(DefaultOutputPath());
    UpdateTitle();

    /* The controls follow the project type of the session. */
    ApplyProjectType(project_type_);

    Bind(wxEVT_MENU, &MainFrame::OnExit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
    Bind(wxEVT_MENU, &MainFrame::OnImportConfiguration, this, kMenuImportConfiguration);
    Bind(wxEVT_MENU, &MainFrame::OnExportConfiguration, this, kMenuExportConfiguration);
    Bind(wxEVT_MENU, &MainFrame::OnImportRegistry, this, kMenuImportRegistry);
    Bind(APPBOX_SIDE_NAV, &MainFrame::OnSideNavChanged, this);
    Bind(wxEVT_TOOL, &MainFrame::OnStartupFiles, this, kToolbarStartupFiles);
    Bind(wxEVT_TOOL, &MainFrame::OnCapture, this, kToolbarCapture);
    Bind(wxEVT_TOOL, &MainFrame::OnBuild, this, kToolbarBuild);
    Bind(wxEVT_TOOL, &MainFrame::OnBuildAndRun, this, kToolbarBuildAndRun);
    Bind(APPBOX_CAPTURE_PROGRESS, &MainFrame::OnCaptureProgress, this);
    Bind(APPBOX_CAPTURE_FINISHED, &MainFrame::OnCaptureFinished, this);
    Bind(APPBOX_CAPTURE_FILL_PROGRESS, &MainFrame::OnFillProgress, this);
    Bind(APPBOX_CAPTURE_FILL_FINISHED, &MainFrame::OnFillFinished, this);
    Bind(APPBOX_REALTIME_CAPTURE_PROGRESS, &MainFrame::OnRealtimeCaptureProgress, this);
    Bind(APPBOX_REALTIME_CAPTURE_STAGE, &MainFrame::OnRealtimeCaptureStage, this);
    Bind(APPBOX_REALTIME_CAPTURE_FINISHED, &MainFrame::OnRealtimeCaptureFinished, this);
    Bind(wxEVT_BUTTON, &MainFrame::OnBrowseOutput, this, kSettingsBrowseOutput);
    Bind(wxEVT_TEXT, &MainFrame::OnOutputPathEdited, this, kSettingsOutputPath);
    Bind(wxEVT_COMBOBOX, &MainFrame::OnProjectTypeChanged, this, kSettingsProjectType);
    Bind(wxEVT_BUTTON, &MainFrame::OnBrowseMetadata, this, kSettingsMetadataBrowse);
    Bind(APPBOX_METADATA_CHANGED, &MainFrame::OnMetadataChanged, this);
    Bind(wxEVT_BUTTON, &MainFrame::OnBrowseStartupImage, this, kSettingsStartupBrowse);
    Bind(APPBOX_STARTUP_CHANGED, &MainFrame::OnStartupScreenChanged, this);
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

    if (capture_thread_.joinable())
    {
        capture_cancelled_ = true;
        capture_thread_.join();
    }

    /*
     * The fill of the workspaces is the phase which cannot be stopped: a window
     * which goes away while it runs waits for it, so the models it produced are
     * not lost half way.
     */
    if (fill_thread_.joinable())
    {
        fill_thread_.join();
    }

    /*
     * The recording is stopped before its thread is joined: the recorder owns
     * the session of the capture and gives it back when it is asked to.
     */
    if (realtime_capture_thread_.joinable())
    {
        realtime_capture_cancelled_ = true;
        realtime_recorder_.RequestStop();
        realtime_capture_thread_.join();
    }

    if (capture_wizard_timer_ != nullptr)
    {
        capture_wizard_timer_->Stop();
        delete capture_wizard_timer_;
        capture_wizard_timer_ = nullptr;
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
    menu_file->Append(kMenuImportRegistry, "&Import Registry...");
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
    toolbar_ = new Toolbar(this, wxID_ANY);

    /*
     * The order of the navigation items is the order of the pages of the
     * workspace book, because the activation of an item selects the page of
     * its index.
     */
    side_nav_ = new SideNav(this, wxID_ANY);
    side_nav_->AddItem("Filesystem", wxART_FOLDER,
                       "Folders and files the packaged application sees, with the isolation mode of every entry");
    side_nav_->AddItem("Registry", wxART_HARDDISK, "Registry the packaged application sees inside the sandbox");
    side_nav_->AddItem("Network", wxART_GO_FORWARD,
                       "Name resolution and proxy the packaged application uses inside the sandbox");
    side_nav_->AddItem("Environment", wxART_LIST_VIEW,
                       "Environment variables the packaged application sees inside the sandbox");
    side_nav_->AddItem("Settings", wxART_HELP,
                       "Archive the Build command writes, the type of the product and the file properties of the "
                       "launcher");
    side_nav_->AddItem("Debug", wxART_EXECUTABLE_FILE,
                       "Runs a program and shows which isolation entry points it really uses");

    workspace_ = new wxSimplebook(this, wxID_ANY);

    filesystem_panel_ = new FilesystemPanel(workspace_, model_, filesystem_isolation_);
    workspace_->AddPage(filesystem_panel_, "Filesystem");

    registry_panel_ = new RegistryPanel(workspace_, registry_model_);
    workspace_->AddPage(registry_panel_, "Registry");

    network_panel_ = new NetworkPanel(workspace_, network_);
    workspace_->AddPage(network_panel_, "Network");

    environment_panel_ = new EnvironmentPanel(workspace_, environment_);
    workspace_->AddPage(environment_panel_, "Environment");

    settings_panel_ = new SettingsPanel(workspace_);
    workspace_->AddPage(settings_panel_, "Settings");

    debug_panel_ = new DebugPanel(workspace_);
    workspace_->AddPage(debug_panel_, "Debug");
    workspace_->SetSelection(static_cast<size_t>(0));

    auto* body = new wxBoxSizer(wxHORIZONTAL);
    body->Add(side_nav_, 0, wxEXPAND);
    body->Add(workspace_, 1, wxEXPAND);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(toolbar_, 0, wxEXPAND);
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
    if (project_type_ == appbox::ProjectType::Patch)
    {
        /* A patch package carries no launcher, so it needs no startup file. */
        const auto files = appbox::ContentFileCount(model_);
        SetStatusText(wxString::Format("Patch project: %llu files", static_cast<unsigned long long>(files)));
        return;
    }

    if (!model_.HasStartupFiles())
    {
        SetStatusText("No startup file selected");
        return;
    }

    const auto& files = model_.StartupFiles();

    int auto_start = 0;
    for (const auto& file : files)
    {
        if (file.auto_start)
        {
            ++auto_start;
        }
    }

    const auto& first = files.front();
    SetStatusText(wxString::Format("Startup files: %d (%d auto start) - first: %s", static_cast<int>(files.size()),
                                   auto_start, first.import_name + L"\\" + first.relative_path));
}

void MainFrame::UpdateTitle()
{
    SetTitle("AppBox [" + OutputPath() + "]");
}

wxString MainFrame::OutputPath() const
{
    if (settings_panel_ != nullptr)
    {
        const auto edited = settings_panel_->GetOutputPath();
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
    if (model_.HasStartupFiles())
    {
        derived.SetName(model_.StartupFiles().front().import_name);
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

void MainFrame::OnProjectTypeChanged(wxCommandEvent& event)
{
    ApplyProjectType(settings_panel_->GetProjectType());
    event.Skip();
}

void MainFrame::ApplyProjectType(appbox::ProjectType type)
{
    project_type_ = type;

    /*
     * The box is written back as well: the type of an imported configuration
     * has to be shown, and a value the session refused must not stay visible.
     */
    settings_panel_->SetProjectType(type);

    /*
     * A patch package carries no launcher, so there is no program to extract and
     * start: the run command is offered for a standalone project only.
     */
    toolbar_->SetBuildAndRunEnabled(type == appbox::ProjectType::Standalone);

    /*
     * A patch package carries no launcher either, so its file properties and
     * its startup screen are not part of the product: the two tabs follow the
     * type.
     */
    settings_panel_->EnableMetadata(type == appbox::ProjectType::Standalone);
    settings_panel_->EnableStartup(type == appbox::ProjectType::Standalone);
    RefreshMetadata();
    RefreshStartupScreen();

    /* Keep following the startup files until the user edits the path. */
    if (!output_path_edited_)
    {
        settings_panel_->SetOutputPath(DefaultOutputPath());
    }

    UpdateStatusBar();
    UpdateTitle();
}

void MainFrame::RefreshMetadata()
{
    if (settings_panel_ == nullptr)
    {
        return;
    }

    /*
     * A patch package carries no launcher, so it carries no file properties
     * either: the tab explains that instead of offering a source which would
     * never be packed.
     */
    if (project_type_ == appbox::ProjectType::Patch)
    {
        metadata_inherited_.clear();
        settings_panel_->SetMetadataSources({}, wxString());
        settings_panel_->SetMetadataValues({}, "A patch package carries no launcher, so it has no file properties.");
        return;
    }

    /*
     * The programs the tab offers are the startup files of the session, with
     * the default source of the session in front of them.
     */
    std::vector<MetadataSource> choices;
    choices.push_back(MetadataSource{ "Automatic - first program marked for auto start", wxString() });

    for (const auto& file : model_.StartupFiles())
    {
        std::wstring path;
        if (!model_.StartupFilePath(file, path))
        {
            continue;
        }

        auto label = wxString(file.import_name) + "\\" + wxString(file.relative_path);
        if (file.auto_start)
        {
            label += " (auto start)";
        }
        choices.push_back(MetadataSource{ label, wxString(path) });
    }

    /*
     * The values of the source program are read again on every refresh, so a
     * source which was updated on disk is picked up; the fields the user
     * edited are applied on top of them.
     */
    const auto source = appbox::MetadataSourcePath(model_, metadata_);

    metadata_inherited_.clear();
    wxString note;
    if (source.empty())
    {
        note = "No program to inherit the file properties from.";
    }
    else
    {
        appbox::ApplicationVersionInfo info;
        std::string                    error;
        if (appbox::ReadApplicationMetadata(source, info, error))
        {
            metadata_inherited_ = info.fields;
            note = "Inherited from " + wxString(source);
        }
        else
        {
            note = "The file properties cannot be read: " + wxString::FromUTF8(error);
        }
    }

    settings_panel_->SetMetadataSources(choices, wxString(metadata_.source));
    settings_panel_->SetMetadataValues(appbox::MergeMetadataFields(metadata_inherited_, metadata_.overrides), note);
}

void MainFrame::RefreshStartupScreen()
{
    if (settings_panel_ == nullptr)
    {
        return;
    }

    /*
     * A patch package carries no launcher, so it has no startup screen: the tab
     * explains that instead of offering a setting which would never be packed.
     */
    if (project_type_ == appbox::ProjectType::Patch)
    {
        settings_panel_->SetStartupScreen(appbox::StartupScreen{},
                                          "A patch package carries no launcher, so it has no startup screen.");
        return;
    }

    /*
     * The note describes what the archive will show, so a path which cannot be
     * decoded is reported while the session is edited and not by the pack run.
     */
    wxString note;
    if (startup_screen_.image.empty())
    {
        note = "The launcher shows the name of the application and the progress of the run.";
    }
    else if (!appbox::IsSupportedStartupImage(startup_screen_.image))
    {
        note = "The image cannot be shown: the launcher decodes PNG, JPEG and BMP files.";
    }
    else
    {
        note = "Shown while the launcher starts the application.";
    }

    settings_panel_->SetStartupScreen(startup_screen_, note);
}

void MainFrame::OnMetadataChanged(wxCommandEvent& event)
{
    /* The tab reports every edit; the state of the session follows it. */
    metadata_.source = settings_panel_->GetMetadataSource().ToStdWstring();
    metadata_.overrides = appbox::DiffMetadataFields(metadata_inherited_, settings_panel_->GetMetadataValues());

    /*
     * The values are resolved again, because another source brings its own
     * fields: the overrides stay while the fields which follow the source are
     * replaced by the ones of the new program.
     */
    RefreshMetadata();

    event.Skip();
}

void MainFrame::OnBrowseMetadata(wxCommandEvent&)
{
    wxFileName current(settings_panel_->GetMetadataSource());

    wxFileDialog dialog(this, "File Properties", current.GetPath(), current.GetFullName(),
                        "Programs (*.exe)|*.exe|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    metadata_.source = dialog.GetPath().ToStdWstring();
    RefreshMetadata();
}

void MainFrame::OnStartupScreenChanged(wxCommandEvent& event)
{
    /* The tab reports every edit; the state of the session follows it. */
    startup_screen_ = settings_panel_->GetStartupScreen();
    RefreshStartupScreen();

    event.Skip();
}

void MainFrame::OnBrowseStartupImage(wxCommandEvent&)
{
    wxFileName current(wxString(startup_screen_.image));

    wxFileDialog dialog(this, "Startup Image", current.GetPath(), current.GetFullName(), StartupImageFilter(),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    startup_screen_.image = dialog.GetPath().ToStdWstring();
    RefreshStartupScreen();
}

void MainFrame::OnExit(wxCommandEvent&)
{
    Close(true);
}

void MainFrame::OnAbout(wxCommandEvent&)
{
    /*
     * The dialog lays out the build information which is compiled into the
     * binary, so it shows what this executable is instead of reading a version
     * file or calling git while it is open.
     */
    AboutDialog dialog(this);
    dialog.ShowModal();
}

void MainFrame::OnImportConfiguration(wxCommandEvent&)
{
    wxFileDialog dialog(this, "Import Configuration", wxEmptyString, wxEmptyString, kProjectFileFilter,
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
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
        const auto answer = wxMessageBox("Importing a configuration replaces the imported folders, the imported "
                                         "files, the isolation modes and the main program selection of the "
                                         "current session.\n\n"
                                         "Continue?",
                                         "Import Configuration", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this);
        if (answer != wxYES)
        {
            return;
        }
    }

    /*
     * The document is read first and applied to the models afterwards, so the
     * configuration of the session is only replaced when the whole file was
     * accepted and every entry of it fits the models.
     */
    appbox::ProjectDocument          document;
    appbox::PackModel                loaded;
    appbox::RegistryModel            loaded_registry;
    appbox::FilesystemIsolationModel loaded_isolation;
    appbox::NetworkModel             loaded_network;
    appbox::EnvironmentModel         loaded_environment;
    appbox::ApplicationMetadata      loaded_metadata;
    appbox::StartupScreen            loaded_startup;
    appbox::ProjectType              loaded_project_type = appbox::ProjectType::Standalone;
    appbox::CompressionLevel         loaded_compression_level = appbox::kDefaultCompressionLevel;
    std::wstring                     output_path;
    std::string                      error;

    if (!appbox::LoadProject(dialog.GetPath().ToStdWstring(), document, error) ||
        !appbox::ApplyProjectDocument(document, loaded, loaded_registry, loaded_isolation, loaded_network,
                                      loaded_environment, loaded_metadata, loaded_startup, loaded_project_type,
                                      loaded_compression_level, output_path, error))
    {
        spdlog::error("importing the configuration failed: {}", error);
        wxMessageBox("The configuration could not be imported:\n\n" + wxString::FromUTF8(error), "Import Configuration",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    model_ = std::move(loaded);
    registry_model_ = std::move(loaded_registry);
    filesystem_isolation_ = std::move(loaded_isolation);
    network_ = std::move(loaded_network);
    environment_ = std::move(loaded_environment);
    metadata_ = std::move(loaded_metadata);
    startup_screen_ = std::move(loaded_startup);
    RefreshStartupScreen();
    filesystem_panel_->RefreshModel();
    registry_panel_->RefreshModel();
    network_panel_->RefreshModel();
    environment_panel_->RefreshModel();

    /*
     * A path recorded by the project file becomes the authoritative archive
     * path; without one the path keeps following the main program.
     */
    if (!output_path.empty())
    {
        output_path_edited_ = true;
        settings_panel_->SetOutputPath(wxString(output_path));
    }
    else
    {
        output_path_edited_ = false;
        settings_panel_->SetOutputPath(DefaultOutputPath());
    }

    /* The type of the document decides which product the next Build writes. */
    ApplyProjectType(loaded_project_type);

    /* The level of the document decides how hard the next Build squeezes the archive. */
    settings_panel_->SetCompressionLevel(loaded_compression_level);

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

    wxFileDialog dialog(this, "Export Configuration", suggested.GetPath(), suggested.GetFullName(), kProjectFileFilter,
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    std::string error;
    const auto  document = appbox::MakeProjectDocument(
        model_, registry_model_, filesystem_isolation_, network_, environment_, metadata_, startup_screen_,
        project_type_, settings_panel_->GetCompressionLevel(), OutputPath().ToStdWstring());
    if (!appbox::SaveProject(document, dialog.GetPath().ToStdWstring(), error))
    {
        spdlog::error("exporting the configuration failed: {}", error);
        wxMessageBox("The configuration could not be exported:\n\n" + wxString::FromUTF8(error), "Export Configuration",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    SetStatusText("Configuration exported to " + dialog.GetPath());
}

void MainFrame::OnImportRegistry(wxCommandEvent&)
{
    wxFileDialog dialog(this, "Import Registry", wxEmptyString, wxEmptyString,
                        "Registry files (*.reg)|*.reg|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    /*
     * The file is parsed and validated before it is applied: a file which
     * cannot be read or which is not a valid `.reg` file leaves the registry
     * of the session untouched.
     */
    std::vector<appbox::RegFileEntry> entries;
    std::string                       error;

    if (!appbox::LoadRegFile(dialog.GetPath().ToStdWstring(), entries, error))
    {
        spdlog::error("importing the registry failed: {}", error);
        wxMessageBox("The registry file could not be imported:\n\n" + wxString::FromUTF8(error), "Import Registry",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    if (!appbox::MergeRegFile(registry_model_, entries, error))
    {
        spdlog::error("merging the registry failed: {}", error);
        wxMessageBox("The registry file could not be applied:\n\n" + wxString::FromUTF8(error), "Import Registry",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    registry_panel_->RefreshModel();
    SetStatusText("Registry imported from " + dialog.GetPath());
}

void MainFrame::OnStartupFiles(wxCommandEvent&)
{
    StartupFilesDialog dialog(this, model_);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    std::string error;
    if (!model_.SetStartupFiles(dialog.Selection(), error))
    {
        wxMessageBox(error, "Startup Files", wxOK | wxICON_ERROR, this);
        return;
    }

    filesystem_panel_->RefreshModel();
    UpdateStatusBar();

    /* The programs the file properties can be inherited from follow the list. */
    RefreshMetadata();

    /* Keep following the startup files until the user edits the path. */
    if (!output_path_edited_)
    {
        settings_panel_->SetOutputPath(DefaultOutputPath());
    }
    UpdateTitle();
}

void MainFrame::OnBrowseOutput(wxCommandEvent&)
{
    wxFileName current(OutputPath());

    wxFileDialog dialog(this, "Output Archive", wxEmptyString, current.GetFullName(), "Zip archive (*.zip)|*.zip",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    output_path_edited_ = true;
    settings_panel_->SetOutputPath(dialog.GetPath());
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
    if (pack_thread_.joinable() || capture_thread_.joinable())
    {
        return;
    }

    /*
     * A patch package carries no launcher, so there is nothing to extract and
     * start. The command is not offered for a patch project; a caller which
     * asks for it anyway is refused here.
     */
    if (project_type_ == appbox::ProjectType::Patch && run_after)
    {
        return;
    }

    const bool standalone = project_type_ == appbox::ProjectType::Standalone;

    /* Only a standalone archive carries a launcher which starts a startup file. */
    if (standalone && !model_.HasStartupFiles())
    {
        wxMessageBox("Select at least one startup file before building.", "Build", wxOK | wxICON_INFORMATION, this);
        return;
    }

    const auto zip_path = OutputPath();
    if (zip_path.empty())
    {
        wxMessageBox("Choose the output archive path first.", "Build", wxOK | wxICON_INFORMATION, this);
        return;
    }

    /*
     * The payload of a standalone archive is the resource of this executable:
     * the launcher program, which carries the two sandbox injection modules as
     * resources of its own. A patch package holds no launcher, so its payload
     * stays empty.
     */
    appbox::PackPayloads payloads;
    if (standalone)
    {
        std::string      error;
        std::string_view launcher;
        if (!appbox::ReadEmbeddedResource(nullptr, IDR_APPBOX_LAUNCHER, launcher, error))
        {
            wxMessageBox("The embedded launcher is unavailable: " + error, "Build", wxOK | wxICON_ERROR, this);
            return;
        }

        payloads.launcher_bytes = launcher.data();
        payloads.launcher_size = launcher.size();
    }

    /*
     * Count the files once so the progress callback has a stable total. The non
     * content entries of the product are added to the files which come from an
     * import, so the packing stage reports this count and the extracting stage
     * of a `Build and Run` run reports the very same number of file entries.
     */
    /*
     * A patch package carries no launcher, so the startup image of the session
     * is an entry of a standalone archive only.
     */
    const auto non_content =
        standalone ? appbox::NonContentArchiveEntryCount(startup_screen_) : appbox::kNonContentPatchEntries;
    const auto total = non_content + appbox::ContentFileCount(model_);
    const auto planned = run_after ? total * 2 : total;
    const auto range = static_cast<int>(std::min<std::size_t>(planned, std::numeric_limits<int>::max()));

    /*
     * The dialog reports the progress of the run and turns into the result
     * display of the same dialog once the run is over. It keeps the elapsed
     * time and the rate of its own updates on its own clock.
     */
    last_progress_ = appbox::BuildProgress{ appbox::BuildStage::Preparing, 0, total, {} };

    ActionDialog::Options options;
    options.description = run_after ? "The archive is written from the imports of the workspace and started afterwards."
                                    : "The archive is written from the imports of the workspace.";
    options.field_labels = { "Files:", "Status:" };
    options.buttons = ActionDialog::Buttons_Cancel;

    progress_dialog_ = new ActionDialog(this, run_after ? "Building and Running" : "Building", options);
    progress_dialog_->Show();

    pack_cancelled_ = false;
    pack_finished_ = false;
    pack_range_ = range;
    pack_phase_total_ = total;
    run_after_pack_ = run_after;

    ApplyPackProgress();
    progress_dialog_->SetField(kBuildFieldStatus, "Running");

    /*
     * The models are copied so a later UI action cannot mutate the archive
     * content while the worker reads it.
     */
    const auto snapshot = model_;
    const auto registry_snapshot = registry_model_;
    const auto isolation_snapshot = filesystem_isolation_;
    const auto network_snapshot = network_;
    const auto environment_snapshot = environment_;
    const auto metadata_snapshot = metadata_;
    const auto startup_snapshot = startup_screen_;
    const auto zip_wide = zip_path.ToStdWstring();
    const auto project_type = project_type_;
    const auto compression_level = settings_panel_->GetCompressionLevel();

    pack_thread_ = std::thread([this, snapshot, registry_snapshot, isolation_snapshot, network_snapshot,
                                environment_snapshot, metadata_snapshot, startup_snapshot, payloads, zip_wide,
                                run_after, project_type, compression_level]() {
        const auto report_progress = [this](const appbox::BuildProgress& report) {
            auto* event = new wxThreadEvent(APPBOX_PACK_PROGRESS);
            event->SetPayload(report);
            this->GetEventHandler()->QueueEvent(event);
            return !this->pack_cancelled_.load();
        };

        /*
         * The extraction reports its own progress, so it is carried into the
         * build report the dialog renders: extracting is the second stage of a
         * `Build and Run` run and continues the bar of the packing stage.
         */
        const auto extract_progress = [&report_progress](const appbox::ExtractProgress& report) {
            return report_progress(
                appbox::BuildProgress{ appbox::BuildStage::Extracting, report.done, report.total, report.current });
        };

        PackOutcome outcome;
        if (project_type == appbox::ProjectType::Patch)
        {
            /* A patch package holds the resources without a launcher. */
            outcome.error = appbox::PackPatch(snapshot, registry_snapshot, isolation_snapshot, network_snapshot,
                                              environment_snapshot, compression_level, zip_wide, report_progress);
        }
        else
        {
            outcome.error = appbox::Pack(snapshot, registry_snapshot, isolation_snapshot, network_snapshot,
                                         environment_snapshot, metadata_snapshot, startup_snapshot, payloads,
                                         compression_level, zip_wide, report_progress);
        }

        if (outcome.error.empty())
        {
            /*
             * The launcher is named after the main program of the snapshot; a
             * patch package has none, because it is not started on its own.
             */
            if (project_type == appbox::ProjectType::Standalone)
            {
                outcome.launcher_entry = appbox::LauncherEntryName(snapshot);
            }
            outcome.archive_path = zip_wide;
        }

        if (outcome.error.empty() && run_after)
        {
            const auto folder = std::filesystem::temp_directory_path() / UniqueExtractFolder();
            const auto extract_error = appbox::ExtractArchive(zip_wide, folder.wstring(), extract_progress);
            if (extract_error != appbox::ZIPREADER_SUCCESS)
            {
                outcome.error = appbox::ZipReaderErrorMessage(extract_error);
            }
            else
            {
                outcome.extract_dir = folder.wstring();
            }
        }

        auto* event = new wxThreadEvent(APPBOX_PACK_FINISHED);
        event->SetPayload(outcome);
        this->GetEventHandler()->QueueEvent(event);
    });
}

void MainFrame::ApplyPackProgress()
{
    if (progress_dialog_ == nullptr || pack_finished_)
    {
        return;
    }

    /*
     * The extracting stage of a `Build and Run` run continues the progress bar
     * where the packing stage ended, so both stages advance a single bar.
     */
    const auto handled = last_progress_.stage == appbox::BuildStage::Extracting
                             ? pack_phase_total_ + last_progress_.done
                             : last_progress_.done;
    const auto value =
        static_cast<int>(std::min<std::size_t>(handled, static_cast<std::size_t>(std::numeric_limits<int>::max())));

    progress_dialog_->SetAction(BuildStageAction(last_progress_.stage));
    progress_dialog_->SetObject(wxString(last_progress_.current));
    progress_dialog_->SetField(kBuildFieldFiles, BuildFilesField(last_progress_));
    progress_dialog_->SetProgress(value, pack_range_);
}

void MainFrame::OnPackProgress(wxThreadEvent& event)
{
    if (progress_dialog_ == nullptr || pack_finished_)
    {
        return;
    }

    last_progress_ = event.GetPayload<appbox::BuildProgress>();

    /* The user dismissed the dialog, so the run has to be aborted. */
    if (progress_dialog_->WasCancelled())
    {
        pack_cancelled_ = true;
        return;
    }

    ApplyPackProgress();
}

void MainFrame::OnPackFinished(wxThreadEvent& event)
{
    if (pack_thread_.joinable())
    {
        pack_thread_.join();
    }

    /* A late progress report must not overwrite the result text below. */
    pack_finished_ = true;

    const auto outcome = event.GetPayload<PackOutcome>();
    const auto run_after = run_after_pack_;
    run_after_pack_ = false;

    auto        result = appbox::BuildOutcome::ArchiveWritten;
    std::string result_error;
    wxString    final_status;

    if (outcome.error == appbox::kBuildCancelledError ||
        outcome.error == appbox::ZipReaderErrorMessage(appbox::ZIPREADER_CANCELLED))
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
         * The launcher carries the file name of the main program, so it is
         * started through the name the pack run resolved.
         */
        long pid = 0;
        if (!outcome.launcher_entry.empty())
        {
            const auto launcher = std::filesystem::path(outcome.extract_dir) / outcome.launcher_entry;
            pid = wxExecute("\"" + wxString(launcher.wstring()) + "\"", wxEXEC_ASYNC);
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
     * The result is presented by the very dialog which reported the progress
     * before: the fields name what the run ended with, and the failure of the
     * run is reported by a message box, like the failure of a capture. The
     * Complete() call waits for the user before the dialog is destroyed.
     */
    progress_dialog_->SetAction(BuildResultAction(result));
    progress_dialog_->SetObject(wxString(outcome.archive_path));
    progress_dialog_->SetField(kBuildFieldStatus, BuildResultStatus(result));
    progress_dialog_->SetProgress(pack_range_, pack_range_);

    if (result == appbox::BuildOutcome::Failed && !result_error.empty())
    {
        wxMessageBox("The build run failed:\n\n" + wxString::FromUTF8(result_error), "Build", wxOK | wxICON_ERROR,
                     this);
    }

    progress_dialog_->Complete();

    progress_dialog_->Destroy();
    progress_dialog_ = nullptr;
}

bool MainFrame::OnSnapshotPathBrowse(appbox::SnapshotBaselineSource source, std::wstring& path)
{
    /*
     * The baseline is a file and not a state in memory, so the user picks where
     * it is kept: the file is what the comparison opens again after the
     * installation, and it can be used by another capture as well.
     */
    const bool existing = source == appbox::SnapshotBaselineSource::Existing;

    const auto   suggested = existing ? wxString() : wxString("snapshot.") + appbox::kSnapshotFileExtension;
    wxFileDialog dialog(this, existing ? "Use Snapshot" : "Create Snapshot", wxEmptyString, suggested,
                        SnapshotFileFilter(),
                        existing ? (wxFD_OPEN | wxFD_FILE_MUST_EXIST) : (wxFD_SAVE | wxFD_OVERWRITE_PROMPT));
    if (dialog.ShowModal() != wxID_OK)
    {
        /* The path the user set is left as it is, so a dismissed dialog is no dead end. */
        return false;
    }

    path = dialog.GetPath().ToStdWstring();
    return true;
}

void MainFrame::OnSnapshotBaselineRequested(appbox::SnapshotBaselineSource source, const std::wstring& path)
{
    if (capture_wizard_ == nullptr || capture_thread_.joinable() || pack_thread_.joinable() ||
        realtime_capture_thread_.joinable())
    {
        return;
    }

    if (source == appbox::SnapshotBaselineSource::Existing)
    {
        /*
         * The file the user named is the baseline, so only its path is kept:
         * the state the file holds is never read into the memory of the
         * packer, and the comparison writes the state after the change into
         * the same file.
         */
        appbox::SnapshotPhaseCounts counts;
        std::string                 error;
        if (!ReadSnapshotBaseline(path, counts, error))
        {
            spdlog::error("the snapshot could not be used: {}", error);
            wxMessageBox("The snapshot could not be used:\n\n" + wxString::FromUTF8(error), "Capture",
                         wxOK | wxICON_ERROR, this);
            capture_wizard_->SetBaselineReady(false);
            return;
        }

        capture_snapshot_path_ = path;
        capture_wizard_->SetBaselineReady(true);
        capture_wizard_->SetProgressStatus(
            appbox::CaptureWizardStage::Baseline, "The baseline was loaded", wxString(path),
            wxString::Format("%llu entries", static_cast<unsigned long long>(counts.files)));
        SetStatusText(wxString::Format("Snapshot loaded from %s (%llu filesystem entries)", wxString(path).c_str(),
                                       static_cast<unsigned long long>(counts.files)));
        return;
    }

    /*
     * The file the state is read into is the one the user named, so it is
     * checked before the walk starts: the machine is read into it afterwards
     * and the comparison opens the same file again after the installation.
     */
    if (!CanWriteSnapshotFile(path, this))
    {
        capture_wizard_->SetBaselineReady(false);
        return;
    }

    StartSnapshotCapture(CapturePhase::Baseline, path);
}

void MainFrame::StartSnapshotComparison()
{
    if (capture_wizard_ == nullptr || capture_thread_.joinable())
    {
        return;
    }

    std::error_code ec;
    if (capture_snapshot_path_.empty() ||
        !std::filesystem::is_regular_file(std::filesystem::path(capture_snapshot_path_), ec) || ec)
    {
        const auto missing = capture_snapshot_path_;
        wxMessageBox("The snapshot file which holds the baseline does not exist anymore:\n\n" + wxString(missing) +
                         "\n\nThe capture cannot compare the machine with it. Ask for the comparison again, or cancel "
                         "the capture and start a new one.",
                     "Capture", wxOK | wxICON_ERROR, this);
        capture_wizard_->SetComparisonFailed();
        return;
    }

    StartSnapshotCapture(CapturePhase::Comparison, capture_snapshot_path_);
}

void MainFrame::StartSnapshotCapture(CapturePhase phase, const std::wstring& path)
{
    if (capture_thread_.joinable() || pack_thread_.joinable() || realtime_capture_thread_.joinable())
    {
        /*
         * The request of the step which asked for the run has to be answered
         * whatever happens, because the step waits for the answer before it can
         * be left: a run which never starts reports a failure of its own.
         */
        if (phase == CapturePhase::Baseline)
        {
            capture_wizard_->SetBaselineReady(false);
        }
        else
        {
            capture_wizard_->SetComparisonFailed();
        }
        return;
    }

    appbox::CaptureOptions options;
    options.filesystem_roots = appbox::DefaultCaptureFilesystemRoots();
    options.registry_roots = appbox::DefaultCaptureRegistryRoots();

    /*
     * The snapshot file may live below a preset directory the walk reads, and
     * it changes while the walk runs, so it is left out of the state: a
     * snapshot never describes itself and the archive never carries it.
     */
    options.excluded_paths.push_back(path);

    if (options.filesystem_roots.empty())
    {
        wxMessageBox("None of the preset directories of the filesystem view is available on this machine.", "Capture",
                     wxOK | wxICON_ERROR, this);

        if (phase == CapturePhase::Baseline)
        {
            capture_wizard_->SetBaselineReady(false);
        }
        else
        {
            capture_wizard_->SetComparisonFailed();
        }
        return;
    }

    const bool comparison = phase == CapturePhase::Comparison;

    capture_phase_ = phase;
    capture_snapshot_path_ = path;
    last_capture_progress_ = appbox::CaptureProgress{};
    capture_cancelled_ = false;
    capture_finished_ = false;

    /*
     * The total number of the entries of a capture is not known before the walk
     * is over, so the bar of the step advances in a cycle instead of a
     * fraction, and the wizard keeps the elapsed time on its own clock.
     */
    capture_wizard_->SetProgressStatus(
        comparison ? appbox::CaptureWizardStage::Comparison : appbox::CaptureWizardStage::Baseline,
        comparison ? "Reading the state of the machine again" : "Reading the state of the machine", wxEmptyString,
        wxEmptyString);

    capture_thread_ = std::thread([this, options, phase, path]() {
        const auto report_progress = [this](const appbox::CaptureProgress& report) {
            auto* event = new wxThreadEvent(APPBOX_CAPTURE_PROGRESS);
            event->SetPayload(report);
            this->GetEventHandler()->QueueEvent(event);
            return !this->capture_cancelled_.load();
        };

        const bool comparison = phase == CapturePhase::Comparison;

        SnapshotCaptureOutcome outcome;
        std::string            error;
        if (!WriteCapturePhase(options, path, comparison, report_progress, error))
        {
            outcome.error = error;
        }
        else if (comparison)
        {
            appbox::SnapshotDiff difference;
            if (!appbox::DiffSnapshotPhases(path, difference, error))
            {
                outcome.error = error;
            }
            else
            {
                outcome.diff = std::make_shared<const appbox::SnapshotDiff>(std::move(difference));
            }
        }
        else
        {
            /*
             * The counts belong to the result of a baseline run. They are read
             * from the file instead of being counted while the state is
             * written, because the file is what the state is.
             */
            const auto reader = appbox::SnapshotReader::Open(path, error);
            if (reader == nullptr || !reader->Counts(appbox::SnapshotPhase::Before, outcome.counts, error))
            {
                spdlog::warn("the entries of the snapshot could not be counted: {}", error);
            }
        }

        auto* event = new wxThreadEvent(APPBOX_CAPTURE_FINISHED);
        event->SetPayload(outcome);
        this->GetEventHandler()->QueueEvent(event);
    });
}

void MainFrame::OnCaptureProgress(wxThreadEvent& event)
{
    if (capture_wizard_ == nullptr || capture_finished_)
    {
        return;
    }

    last_capture_progress_ = event.GetPayload<appbox::CaptureProgress>();

    /* The user dismissed the wizard, so the run has to be aborted. */
    if (capture_wizard_->WasCancelled())
    {
        capture_cancelled_ = true;
        return;
    }

    ApplyCaptureProgress();
}

void MainFrame::ApplyCaptureProgress()
{
    if (capture_wizard_ == nullptr || capture_finished_)
    {
        return;
    }

    capture_wizard_->SetProgressStatus(
        capture_phase_ == CapturePhase::Baseline ? appbox::CaptureWizardStage::Baseline
                                                 : appbox::CaptureWizardStage::Comparison,
        CaptureStageAction(last_capture_progress_.stage), wxString(last_capture_progress_.current),
        CaptureEntriesField(last_capture_progress_));
}

void MainFrame::OnCaptureFinished(wxThreadEvent& event)
{
    if (capture_thread_.joinable())
    {
        capture_thread_.join();
    }

    /* A late progress report must not touch the wizard anymore. */
    capture_finished_ = true;

    if (capture_wizard_ == nullptr)
    {
        return;
    }

    if (capture_wizard_->WasCancelled())
    {
        /* The user dropped the capture, so the result of the run is not used. */
        capture_cancelled_ = true;
        CloseCaptureWizard();
        SetStatusText("Snapshot capture cancelled");
        return;
    }

    const auto outcome = event.GetPayload<SnapshotCaptureOutcome>();
    const bool baseline = capture_phase_ == CapturePhase::Baseline;

    if (outcome.error == appbox::kCaptureCancelledError)
    {
        SetStatusText("Capture cancelled");

        /* The step offers its work again, so a run which was aborted is no dead end. */
        if (baseline)
        {
            capture_wizard_->SetBaselineReady(false);
        }
        else
        {
            capture_wizard_->SetComparisonFailed();
        }
        return;
    }

    if (!outcome.error.empty())
    {
        spdlog::error("the capture failed: {}", outcome.error);
        wxMessageBox("The state of the machine could not be read:\n\n" + wxString::FromUTF8(outcome.error), "Capture",
                     wxOK | wxICON_ERROR, this);

        if (baseline)
        {
            capture_wizard_->SetBaselineReady(false);
        }
        else
        {
            capture_wizard_->SetComparisonFailed();
        }
        return;
    }

    if (baseline)
    {
        /* The file which was written is the baseline the comparison opens. */
        capture_wizard_->SetBaselineReady(true);
        capture_wizard_->SetProgressStatus(
            appbox::CaptureWizardStage::Baseline, "The baseline is ready", wxString(capture_snapshot_path_),
            wxString::Format("%llu entries", static_cast<unsigned long long>(outcome.counts.files)));
        SetStatusText("Baseline captured into " + wxString(capture_snapshot_path_));
        return;
    }

    /*
     * The comparison changes nothing on its own: the difference is what the
     * last step of the wizard writes into the workspaces, so it is kept until
     * the fill is over.
     */
    capture_diff_ = outcome.diff;
    capture_wizard_->SetAnalysisReady();
}

void MainFrame::OnCapture(wxCommandEvent&)
{
    if (capture_thread_.joinable() || pack_thread_.joinable() || realtime_capture_thread_.joinable() ||
        fill_thread_.joinable() || capture_wizard_ != nullptr)
    {
        return;
    }

    /*
     * The prerequisites are shown by the first step of the wizard instead of
     * being checked before it opens: a machine which cannot record explains
     * itself, and a process which is not elevated is offered the restart.
     */
    appbox::CaptureRequirements requirements;
    requirements.supported = appbox::RealtimeCaptureSupported();
    requirements.elevated = appbox::IsProcessElevated();

    realtime_capture_options_ = appbox::DefaultRealtimeCaptureOptions();
    realtime_capture_cancelled_ = false;
    realtime_capture_finishing_ = false;
    realtime_capture_finished_ = false;
    capture_cancelled_ = false;
    capture_finished_ = false;
    capture_diff_.reset();
    capture_report_ = {};
    capture_snapshot_path_.clear();

    capture_wizard_ = new CaptureWizard(this, "Capture", requirements);
    capture_wizard_->SetStageHandler(
        [this](appbox::CaptureWizardStage from, appbox::CaptureWizardStage to) { OnCaptureStageChanged(from, to); });
    capture_wizard_->SetBaselineHandler([this](appbox::SnapshotBaselineSource source, const std::wstring& path) {
        OnSnapshotBaselineRequested(source, path);
    });
    capture_wizard_->SetBrowseHandler([this](appbox::SnapshotBaselineSource source, std::wstring& path) {
        return OnSnapshotPathBrowse(source, path);
    });
    capture_wizard_->Show();

    /*
     * A quiet machine reports no progress, so the wizard is polled on a timer
     * of its own: a wizard the user dismissed ends the run in flight, and one
     * whose work is over is destroyed.
     */
    capture_wizard_timer_ = new wxTimer(this);
    Bind(wxEVT_TIMER, &MainFrame::OnCaptureWizardTick, this, capture_wizard_timer_->GetId());
    capture_wizard_timer_->Start(250 /* milliseconds */);
}

void MainFrame::StartRealtimeCaptureRecording()
{
    capture_wizard_->SetProgressStatus(appbox::CaptureWizardStage::Recording, "Preparing the recording", wxEmptyString,
                                       wxEmptyString);

    realtime_capture_thread_ = std::thread([this]() {
        RealtimeCaptureOutcome outcome;

        /*
         * The baseline of the environment is read before the session starts,
         * so nothing the recording itself does can slip into the difference.
         */
        std::vector<appbox::SnapshotEnvironmentEntry> environment_before;
        std::string                                   error;
        if (!appbox::ReadPersistedEnvironment(environment_before, error))
        {
            outcome.error = error;
        }
        else if (!realtime_recorder_.Start(
                     realtime_capture_options_,
                     [this](const appbox::RealtimeCaptureProgress& report) {
                         auto* event = new wxThreadEvent(APPBOX_REALTIME_CAPTURE_PROGRESS);
                         event->SetPayload(report);
                         this->GetEventHandler()->QueueEvent(event);
                         return !this->realtime_capture_cancelled_.load();
                     },
                     error))
        {
            outcome.error = error;
        }
        else
        {
            auto* stage = new wxThreadEvent(APPBOX_REALTIME_CAPTURE_STAGE);
            stage->SetPayload(kRealtimeStageRecording);
            this->GetEventHandler()->QueueEvent(stage);

            /*
             * The recording ends when the user left the step of the recording
             * or dismissed the wizard: the session is stopped, which ends the
             * consumer of the recorder on its own.
             */
            while (!realtime_capture_finishing_.load() && !realtime_capture_cancelled_.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            realtime_recorder_.RequestStop();
            realtime_recorder_.Join(60000 /* milliseconds */);

            if (!realtime_capture_cancelled_.load())
            {
                auto* analyzing = new wxThreadEvent(APPBOX_REALTIME_CAPTURE_STAGE);
                analyzing->SetPayload(kRealtimeStageAnalyzing);
                this->GetEventHandler()->QueueEvent(analyzing);

                /*
                 * The analysis follows the semantics of the snapshot capture:
                 * the recorded changes become the difference of the machine,
                 * which the last step of the wizard writes into the workspaces.
                 * The changes which belong to no preset directory are reported
                 * with it, so the dialog lists them as well.
                 */
                const auto events = realtime_recorder_.TakeEvents();

                appbox::SnapshotDiff difference;
                std::string          harvest_error;
                if (!appbox::HarvestRealtimeEvents(events, environment_before, realtime_capture_options_, difference,
                                                   outcome.report, harvest_error))
                {
                    outcome.error = harvest_error;
                }
                else
                {
                    outcome.diff = std::make_shared<const appbox::SnapshotDiff>(std::move(difference));
                }
            }
        }

        auto* event = new wxThreadEvent(APPBOX_REALTIME_CAPTURE_FINISHED);
        event->SetPayload(outcome);
        this->GetEventHandler()->QueueEvent(event);
    });
}

void MainFrame::OnCaptureStageChanged(appbox::CaptureWizardStage from, appbox::CaptureWizardStage to)
{
    if (capture_wizard_ == nullptr)
    {
        return;
    }

    if (to == appbox::CaptureWizardStage::Recording)
    {
        StartRealtimeCaptureRecording();
        return;
    }

    if (to == appbox::CaptureWizardStage::Comparison)
    {
        StartSnapshotComparison();
        return;
    }

    if (to == appbox::CaptureWizardStage::Review && from == appbox::CaptureWizardStage::Recording)
    {
        /*
         * The recording ends on the decision of the user: the flag hands the
         * capture thread over to the analysis of the recorded changes, which
         * the wizard follows on its last step. The difference of a snapshot
         * capture is already there when its last step is entered, so only the
         * recording is stopped here.
         */
        realtime_capture_finishing_ = true;
        capture_wizard_->SetAnalysisStatus("Stopping the session of the capture");
        return;
    }

    if (to == appbox::CaptureWizardStage::Review)
    {
        /* The difference of the snapshot capture is there, so the last phase of the capture runs. */
        StartFill();
    }
}

void MainFrame::StartFill()
{
    if (capture_diff_ == nullptr || capture_wizard_ == nullptr || fill_thread_.joinable())
    {
        return;
    }

    /*
     * The models are copied, so the run reads and writes a state of its own
     * while the workspaces keep showing the state the user has: the copies are
     * swapped in once the run is over.
     */
    auto diff = capture_diff_;
    auto model = model_;
    auto registry = registry_model_;
    auto environment = environment_;
    auto report = capture_report_;

    capture_wizard_->SetAnalysisStatus("Writing the changes of the capture into the workspaces");
    capture_wizard_->SetFillRunning(true);

    fill_thread_ = std::thread([this, diff, model, registry, environment, report]() mutable {
        CaptureFillOutcome outcome;

        /*
         * The dialog shows the entry which is being written, so the reports are
         * throttled: the fill reports every entry twice and a capture of a whole
         * machine walks through thousands of them.
         */
        appbox::ActionThrottle      throttle{ std::chrono::milliseconds(kFillReportInterval) };
        appbox::CaptureFillProgress last;

        const auto progress = [this, &throttle, &last](const appbox::CaptureFillProgress& current) {
            last = current;
            if (!throttle.TryAcquire())
            {
                return;
            }

            auto* event = new wxThreadEvent(APPBOX_CAPTURE_FILL_PROGRESS);
            event->SetPayload(current);
            this->GetEventHandler()->QueueEvent(event);
        };

        std::string error;
        if (!appbox::ApplySnapshotDiff(*diff, model, registry, environment, report, progress, error))
        {
            outcome.error = error;
        }
        else
        {
            outcome.applied = true;
            outcome.model = std::move(model);
            outcome.registry = std::move(registry);
            outcome.environment = std::move(environment);
        }

        outcome.report = std::move(report);

        /*
         * The state of the entry which was written last is shown whatever the
         * throttle decided, so the row never names an entry the run left behind.
         */
        auto* state = new wxThreadEvent(APPBOX_CAPTURE_FILL_PROGRESS);
        state->SetPayload(last);
        this->GetEventHandler()->QueueEvent(state);

        auto* finished = new wxThreadEvent(APPBOX_CAPTURE_FILL_FINISHED);
        finished->SetPayload(std::move(outcome));
        this->GetEventHandler()->QueueEvent(finished);
    });
}

void MainFrame::OnFillProgress(wxThreadEvent& event)
{
    if (capture_wizard_ == nullptr)
    {
        return;
    }

    capture_wizard_->SetFillProgress(event.GetPayload<appbox::CaptureFillProgress>());
}

void MainFrame::OnFillFinished(wxThreadEvent& event)
{
    if (fill_thread_.joinable())
    {
        fill_thread_.join();
    }

    if (capture_wizard_ == nullptr)
    {
        return;
    }

    auto outcome = event.GetPayload<CaptureFillOutcome>();

    if (outcome.applied)
    {
        /*
         * The run wrote copies of the models, so the session receives them here:
         * the workspaces show the state of the capture from now on.
         */
        model_ = std::move(outcome.model);
        registry_model_ = std::move(outcome.registry);
        environment_ = std::move(outcome.environment);

        filesystem_panel_->RefreshModel();
        registry_panel_->RefreshModel();
        environment_panel_->RefreshModel();

        SetStatusText("The changes of the capture were written into the workspaces");
    }
    else
    {
        spdlog::error("the changes of the capture could not be written: {}", outcome.error);
        appbox::AddErrorNotice(outcome.report, appbox::CaptureFillDomain::Filesystem,
                               L"the changes of the capture could not be written: " +
                                   appbox::UTF8ToWide(outcome.error));
    }

    /*
     * The whole work of the last step is over, whatever the run did: the entries
     * which were not written are listed and the button which ends the capture is
     * offered.
     */
    capture_report_ = std::move(outcome.report);
    capture_wizard_->SetFillReport(capture_report_);
    capture_wizard_->MarkFillComplete();
}

void MainFrame::CloseCaptureWizard()
{
    if (capture_wizard_timer_ != nullptr)
    {
        capture_wizard_timer_->Stop();
        delete capture_wizard_timer_;
        capture_wizard_timer_ = nullptr;
    }

    if (capture_wizard_ != nullptr)
    {
        capture_wizard_->Destroy();
        capture_wizard_ = nullptr;
    }

    capture_diff_.reset();
    capture_report_ = {};
}

void MainFrame::OnCaptureWizardTick(wxTimerEvent&)
{
    if (capture_wizard_ == nullptr)
    {
        return;
    }

    /* A quiet machine reports no progress, so the state of the wizard is
     * polled instead. */
    if (capture_wizard_->WasCancelled())
    {
        capture_cancelled_ = true;
        realtime_capture_cancelled_ = true;

        /* A run in flight reports its end on its own. */
        if (!capture_thread_.joinable() && !realtime_capture_thread_.joinable() && !fill_thread_.joinable())
        {
            CloseCaptureWizard();
            SetStatusText("Capture cancelled");
        }
        return;
    }

    /*
     * The work of the last step is over when the button which confirms the
     * capture is offered, so the wizard is only destroyed here: the runs are
     * over by then, because the difference it was built from came from one of
     * them.
     */
    if (capture_wizard_->WasConfirmed() && !capture_thread_.joinable() && !realtime_capture_thread_.joinable() &&
        !fill_thread_.joinable())
    {
        CloseCaptureWizard();
    }
}

void MainFrame::OnRealtimeCaptureProgress(wxThreadEvent& event)
{
    if (capture_wizard_ == nullptr || realtime_capture_finished_)
    {
        return;
    }

    last_realtime_progress_ = event.GetPayload<appbox::RealtimeCaptureProgress>();

    /* The user dismissed the wizard, so the recording has to be aborted. */
    if (capture_wizard_->WasCancelled())
    {
        realtime_capture_cancelled_ = true;
        return;
    }

    capture_wizard_->SetProgressStatus(
        appbox::CaptureWizardStage::Recording, "Recording the changes of the machine",
        wxString(last_realtime_progress_.process) + "  " + wxString(last_realtime_progress_.current),
        wxString::Format("%llu entries", static_cast<unsigned long long>(last_realtime_progress_.events)));
}

void MainFrame::OnRealtimeCaptureStage(wxThreadEvent& event)
{
    if (capture_wizard_ == nullptr || realtime_capture_finished_)
    {
        return;
    }

    if (event.GetPayload<int>() == kRealtimeStageRecording)
    {
        capture_wizard_->SetProgressStatus(appbox::CaptureWizardStage::Recording,
                                           "Recording the changes of the machine", wxEmptyString, "0 entries");
    }
    else
    {
        capture_wizard_->SetAnalysisStatus("Analyzing the recorded changes of the capture");
    }
}

void MainFrame::OnRealtimeCaptureFinished(wxThreadEvent& event)
{
    if (realtime_capture_thread_.joinable())
    {
        realtime_capture_thread_.join();
    }

    /* A late progress report must not touch the wizard anymore. */
    realtime_capture_finished_ = true;

    const auto outcome = event.GetPayload<RealtimeCaptureOutcome>();

    if (realtime_capture_cancelled_.load())
    {
        CloseCaptureWizard();
        SetStatusText("Realtime capture cancelled");
        return;
    }

    if (!outcome.error.empty() || capture_wizard_ == nullptr)
    {
        spdlog::error("the realtime capture failed: {}", outcome.error);
        wxMessageBox("The changes of the machine could not be recorded:\n\n" + wxString::FromUTF8(outcome.error),
                     "Capture", wxOK | wxICON_ERROR, this);
        CloseCaptureWizard();
        return;
    }

    /*
     * The recording changes nothing on its own: the difference is what the last
     * step of the wizard writes into the workspaces, and the changes which
     * belong to no preset directory are listed next to the entries the fill
     * leaves out.
     */
    capture_diff_ = outcome.diff;
    capture_report_ = outcome.report;
    capture_wizard_->SetFillReport(capture_report_);
    StartFill();
}
