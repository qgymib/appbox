#include "MainFrame.hpp"
#include "AboutDialog.hpp"
#include "ActionDialog.hpp"
#include "EnvironmentPanel.hpp"
#include "FilesystemPanel.hpp"
#include "StartupFilesDialog.hpp"
#include "NetworkPanel.hpp"
#include "RegistryPanel.hpp"
#include "SettingsPanel.hpp"
#include "SideNav.hpp"
#include "Toolbar.hpp"
#include "DebugPanel.hpp"
#include "core/BuildReport.hpp"
#include "core/EmbeddedResource.hpp"
#include "core/EmbeddedResourceIds.h"
#include "core/PackService.hpp"
#include "core/PresetDirectory.hpp"
#include "core/ProjectFile.hpp"
#include "core/RegFile.hpp"
#include "core/ZipReader.hpp"
#include "WString.hpp"
#include <wx/artprov.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/icon.h>
#include <wx/msgdlg.h>
#include <wx/simplebook.h>
#include <wx/strconv.h>
#include <wx/utils.h>
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
const int kMenuImportRegistry = wxNewId();
const int kMenuSaveSnapshotAs = wxNewId();
const int kMenuLoadSnapshot = wxNewId();

/**
 * @brief Index of the entries field of the capture dialog.
 */
constexpr std::size_t kCaptureFieldEntries = 0;

/**
 * @brief Index of the status field of the capture dialog.
 */
constexpr std::size_t kCaptureFieldStatus = 1;

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
    Bind(wxEVT_MENU, &MainFrame::OnSaveSnapshotAs, this, kMenuSaveSnapshotAs);
    Bind(wxEVT_MENU, &MainFrame::OnLoadSnapshot, this, kMenuLoadSnapshot);
    Bind(APPBOX_SIDE_NAV, &MainFrame::OnSideNavChanged, this);
    Bind(wxEVT_TOOL, &MainFrame::OnStartupFiles, this, kToolbarStartupFiles);
    Bind(wxEVT_TOOL, &MainFrame::OnCaptureBefore, this, kToolbarCaptureBefore);
    Bind(wxEVT_TOOL, &MainFrame::OnCaptureAndDiff, this, kToolbarCaptureAndDiff);
    Bind(wxEVT_TOOL, &MainFrame::OnBuild, this, kToolbarBuild);
    Bind(wxEVT_TOOL, &MainFrame::OnBuildAndRun, this, kToolbarBuildAndRun);
    Bind(APPBOX_CAPTURE_PROGRESS, &MainFrame::OnCaptureProgress, this);
    Bind(APPBOX_CAPTURE_FINISHED, &MainFrame::OnCaptureFinished, this);
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
    menu_file->Append(kMenuSaveSnapshotAs, "&Save Snapshot As...");
    menu_file->Append(kMenuLoadSnapshot, "&Load Snapshot...");
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
     * The payloads of a standalone archive are the resources of this
     * executable: the launcher program and the two sandbox injection modules,
     * which the archive carries below `app`. A patch package holds none of
     * them, so its payloads stay empty.
     */
    appbox::PackPayloads payloads;
    if (standalone)
    {
        std::string      error;
        std::string_view launcher;
        std::string_view sandbox32;
        std::string_view sandbox64;
        if (!appbox::ReadEmbeddedResource(nullptr, IDR_APPBOX_LAUNCHER, launcher, error) ||
            !appbox::ReadEmbeddedResource(nullptr, IDR_APPBOX_SANDBOX32, sandbox32, error) ||
            !appbox::ReadEmbeddedResource(nullptr, IDR_APPBOX_SANDBOX64, sandbox64, error))
        {
            wxMessageBox("The embedded payloads are unavailable: " + error, "Build", wxOK | wxICON_ERROR, this);
            return;
        }

        payloads.launcher_bytes = launcher.data();
        payloads.launcher_size = launcher.size();
        payloads.sandbox32_bytes = sandbox32.data();
        payloads.sandbox32_size = sandbox32.size();
        payloads.sandbox64_bytes = sandbox64.data();
        payloads.sandbox64_size = sandbox64.size();
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

void MainFrame::OnCaptureBefore(wxCommandEvent&)
{
    /*
     * The baseline is a file and not a state in memory, so the user picks
     * where it is kept before the machine is read: the file is what a later
     * comparison opens again, and it can be loaded by another session as well.
     */
    const auto   suggested = wxString("snapshot.") + appbox::kSnapshotFileExtension;
    wxFileDialog dialog(this, "Capture Before", wxEmptyString, suggested, SnapshotFileFilter(),
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    StartCapture(false, dialog.GetPath().ToStdWstring());
}

void MainFrame::OnCaptureAndDiff(wxCommandEvent&)
{
    if (snapshot_path_.empty())
    {
        wxMessageBox("There is no baseline to compare the machine with.\n\n"
                     "Use 'Capture Before' to keep the state of the machine in a snapshot file first, or load a "
                     "snapshot with 'File -> Load Snapshot...'.",
                     "Capture and Diff", wxOK | wxICON_INFORMATION, this);
        return;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::path(snapshot_path_), ec) || ec)
    {
        const auto missing = snapshot_path_;
        snapshot_path_.clear();

        wxMessageBox("The snapshot file which holds the baseline does not exist anymore:\n\n" + wxString(missing) +
                         "\n\nUse 'Capture Before' to keep the state of the machine in a snapshot file first, or load "
                         "a snapshot with 'File -> Load Snapshot...'.",
                     "Capture and Diff", wxOK | wxICON_ERROR, this);
        return;
    }

    StartCapture(true, snapshot_path_);
}

void MainFrame::StartCapture(bool diff, const std::wstring& path)
{
    if (capture_thread_.joinable() || pack_thread_.joinable())
    {
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
        return;
    }

    capture_path_ = path;
    last_capture_progress_ = appbox::CaptureProgress{};
    capture_cancelled_ = false;
    capture_finished_ = false;

    /*
     * The total number of the entries of a capture is not known before the
     * walk is over, so the bar advances in a cycle instead of a fraction. The
     * dialog keeps the elapsed time and the cycle of its bar on its own clock.
     */
    ActionDialog::Options dialog_options;
    dialog_options.description = diff
                                     ? "The state of the machine is read and compared with the baseline of the session."
                                     : "The state of the machine is read and written into the snapshot file.";
    dialog_options.mode = ActionDialog::ProgressMode::Indeterminate;
    dialog_options.field_labels = { "Entries:", "Status:" };
    dialog_options.buttons = ActionDialog::Buttons_Cancel;

    capture_dialog_ =
        new ActionDialog(this, diff ? "Capturing and Comparing" : "Capturing the System State", dialog_options);
    capture_dialog_->Show();

    ApplyCaptureProgress();
    capture_dialog_->SetField(kCaptureFieldStatus, "Running");

    /*
     * The models are copied, so the worker thread never reads or writes the
     * state the workspace shows: a comparison is applied to the copies, which
     * the window adopts once the run is over.
     */
    const auto model = model_;
    const auto registry = registry_model_;
    const auto environment = environment_;

    capture_thread_ = std::thread([this, options, diff, path, model, registry, environment]() {
        const auto report_progress = [this](const appbox::CaptureProgress& report) {
            auto* event = new wxThreadEvent(APPBOX_CAPTURE_PROGRESS);
            event->SetPayload(report);
            this->GetEventHandler()->QueueEvent(event);
            return !this->capture_cancelled_.load();
        };

        CaptureOutcome outcome;
        outcome.model = model;
        outcome.registry = registry;
        outcome.environment = environment;

        std::string error;
        if (!WriteCapturePhase(options, path, diff, report_progress, error))
        {
            outcome.error = error;
        }
        else if (diff)
        {
            appbox::SnapshotDiff difference;
            if (!appbox::DiffSnapshotPhases(path, difference, error))
            {
                outcome.error = error;
            }
            else
            {
                appbox::SnapshotApplyReport report;
                std::string                 apply_error;
                if (!appbox::ApplySnapshotDiff(difference, outcome.model, outcome.registry, outcome.environment, report,
                                               apply_error))
                {
                    outcome.error = apply_error;
                }
                else
                {
                    outcome.applied = true;
                    outcome.summary = appbox::SnapshotApplySummary(difference, report);
                }
            }
        }

        /*
         * The counts belong to the result of a baseline run. They are read
         * from the file instead of being counted while the state is written,
         * because the file is what the state is.
         */
        if (outcome.error.empty() && !diff)
        {
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
    if (capture_dialog_ == nullptr || capture_finished_)
    {
        return;
    }

    last_capture_progress_ = event.GetPayload<appbox::CaptureProgress>();

    /* The user dismissed the dialog, so the run has to be aborted. */
    if (capture_dialog_->WasCancelled())
    {
        capture_cancelled_ = true;
        return;
    }

    ApplyCaptureProgress();
}

void MainFrame::ApplyCaptureProgress()
{
    if (capture_dialog_ == nullptr || capture_finished_)
    {
        return;
    }

    capture_dialog_->SetAction(CaptureStageAction(last_capture_progress_.stage));
    capture_dialog_->SetObject(wxString(last_capture_progress_.current));
    capture_dialog_->SetField(kCaptureFieldEntries, CaptureEntriesField(last_capture_progress_));
}

void MainFrame::OnCaptureFinished(wxThreadEvent& event)
{
    if (capture_thread_.joinable())
    {
        capture_thread_.join();
    }

    /* A late progress report must not touch the dialog anymore. */
    capture_finished_ = true;

    if (capture_dialog_ != nullptr)
    {
        capture_dialog_->Destroy();
        capture_dialog_ = nullptr;
    }

    const auto path = capture_path_;
    capture_path_.clear();

    const auto outcome = event.GetPayload<CaptureOutcome>();

    if (outcome.error == appbox::kCaptureCancelledError)
    {
        SetStatusText("Capture cancelled");
        return;
    }
    if (!outcome.error.empty())
    {
        spdlog::error("the capture failed: {}", outcome.error);
        wxMessageBox("The state of the machine could not be read:\n\n" + wxString::FromUTF8(outcome.error), "Capture",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    if (!outcome.applied)
    {
        /* The file which was written is the baseline the next comparison opens. */
        snapshot_path_ = path;
        SetStatusText("Baseline captured into " + wxString(path));

        wxMessageBox(wxString::Format("The state of the machine was written into\n\n  %s\n\n"
                                      "and kept as the baseline of the next comparison:\n\n"
                                      "  filesystem entries: %llu\n"
                                      "  registry keys: %llu\n"
                                      "  registry values: %llu\n"
                                      "  environment variables: %llu",
                                      path.c_str(), static_cast<unsigned long long>(outcome.counts.files),
                                      static_cast<unsigned long long>(outcome.counts.registry_keys),
                                      static_cast<unsigned long long>(outcome.counts.registry_values),
                                      static_cast<unsigned long long>(outcome.counts.environment)),
                     "Capture Before", wxOK | wxICON_INFORMATION, this);
        return;
    }

    model_ = outcome.model;
    registry_model_ = outcome.registry;
    environment_ = outcome.environment;
    filesystem_panel_->RefreshModel();
    registry_panel_->RefreshModel();
    environment_panel_->RefreshModel();

    /*
     * The comparison does not become the baseline of the next one: the file
     * holds a state the machine has already left behind, so the user takes a
     * new baseline before the next comparison.
     */
    snapshot_path_.clear();
    SetStatusText("The difference of the capture was written into the workspaces");
    wxMessageBox(wxString(outcome.summary) +
                     wxString::Format("\n\nThe state after the change was written into\n\n  %s\n\n"
                                      "Take a new 'Capture Before' before the next comparison.",
                                      path.c_str()),
                 "Capture and Diff", wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnSaveSnapshotAs(wxCommandEvent&)
{
    if (snapshot_path_.empty())
    {
        wxMessageBox("There is no snapshot to save.\n\n"
                     "Use 'Capture Before' to keep the state of the machine in a snapshot file first.",
                     "Save Snapshot As", wxOK | wxICON_INFORMATION, this);
        return;
    }

    const auto   suggested = wxString("snapshot.") + appbox::kSnapshotFileExtension;
    wxFileDialog dialog(this, "Save Snapshot As", wxEmptyString, suggested, SnapshotFileFilter(),
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    /*
     * Only the state before the change is written: the copy is the baseline
     * alone, and the session keeps working with the file of its own.
     */
    std::string error;
    if (!appbox::CopySnapshotPhase(snapshot_path_, appbox::SnapshotPhase::Before, dialog.GetPath().ToStdWstring(),
                                   error))
    {
        spdlog::error("saving the snapshot failed: {}", error);
        wxMessageBox("The snapshot could not be saved:\n\n" + wxString::FromUTF8(error), "Save Snapshot As",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    SetStatusText("Snapshot saved to " + dialog.GetPath());
}

void MainFrame::OnLoadSnapshot(wxCommandEvent&)
{
    wxFileDialog dialog(this, "Load Snapshot", wxEmptyString, wxEmptyString, SnapshotFileFilter(),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    const auto fail = [this](const std::string& reason) {
        spdlog::error("loading the snapshot failed: {}", reason);
        wxMessageBox("The snapshot could not be loaded:\n\n" + wxString::FromUTF8(reason), "Load Snapshot",
                     wxOK | wxICON_ERROR, this);
    };

    /*
     * Only the path is kept: the file itself is the baseline, so the entries
     * of the state are never read into the memory of the packer. The state
     * after the change of the file is not used either, because the comparison
     * writes it again.
     */
    const auto path = dialog.GetPath().ToStdWstring();

    std::string error;
    const auto  reader = appbox::SnapshotReader::Open(path, error);
    if (reader == nullptr)
    {
        fail(error);
        return;
    }

    bool present = false;
    if (!reader->HasPhase(appbox::SnapshotPhase::Before, present, error))
    {
        fail(error);
        return;
    }
    if (!present)
    {
        fail("the snapshot file does not hold the state before the change: " + appbox::WideToUTF8(path));
        return;
    }

    appbox::SnapshotPhaseCounts counts;
    if (!reader->Counts(appbox::SnapshotPhase::Before, counts, error))
    {
        fail(error);
        return;
    }

    snapshot_path_ = path;
    SetStatusText(wxString::Format("Snapshot loaded from %s (%llu filesystem entries)", dialog.GetPath().c_str(),
                                   static_cast<unsigned long long>(counts.files)));
}
