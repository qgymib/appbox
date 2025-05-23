#include <wx/wx.h>
#include <wx/filedlg.h>
#include <wx/notebook.h>
#include <wx/wfstream.h>
#include <spdlog/spdlog.h>
#include "widgets/FilePanel.hpp"
#include "widgets/RegistryPanel.hpp"
#include "widgets/SettingsPanel.hpp"
#include "widgets/ProcessDialog.hpp"
#include "StartupFilesDialog.hpp"
#include "LogPanel.hpp"
#include "utils/file.hpp"
#include "utils/wstring.hpp"
#include "MainFrame.hpp"
#include "Common.hpp"

struct MainFrame::Data
{
    Data(MainFrame* owner);
    void OnProcessButtonClick(const wxCommandEvent&);
    void OnStartupFilesButtonClick(const wxCommandEvent&);
    void OnProjectSave(const wxCommandEvent&);
    void OnProjectOpen(const wxCommandEvent&);

    MainFrame*                 mOwner;
    FilePanel*                 mFilePanel;
    SettingsPanel*             mSettingsPanel;
    StartupFilesDialog::Config mStartupFiles;
};

static void s_append_startup_choice(wxArrayString& choice, const FileDataView::Filesystem& fs)
{
    wxString path = wxString::FromUTF8(fs.sandboxPath);
    if (path.EndsWith(L".exe"))
    {
        choice.Add(wxString::FromUTF8(fs.sandboxPath));
    }

    for (auto it = fs.children.begin(); it != fs.children.end(); ++it)
    {
        s_append_startup_choice(choice, *it);
    }
}

static wxArrayString s_get_startup_choices(const FilePanel::Config& config)
{
    wxArrayString choices;
    for (auto it = config.fs.begin(); it != config.fs.end(); ++it)
    {
        s_append_startup_choice(choices, *it);
    }
    return choices;
}

static void s_setup_menubar(MainFrame* owner)
{
    wxMenuBar* menuBar = new wxMenuBar;
    {
        wxMenu* menuFile = new wxMenu;
        menuFile->Append(wxID_OPEN);
        menuFile->Append(wxID_SAVE);
        menuFile->Append(wxID_EXIT);
        menuBar->Append(menuFile, _("File"));
    }
    {
        wxMenu* menuHelp = new wxMenu;
        menuHelp->Append(wxID_ABOUT);
        menuBar->Append(menuHelp, _("Help"));
    }
    owner->SetMenuBar(menuBar);
}

MainFrame::Data::Data(MainFrame* owner)
{
    mOwner = owner;
    s_setup_menubar(owner);
    owner->SetSize(wxDefaultCoord, wxDefaultCoord, 800, 600, wxSIZE_AUTO);

    wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);
    {
        wxNotebook* noteBook = new wxNotebook(owner, wxID_ANY);
        mFilePanel = new FilePanel(noteBook);
        noteBook->AddPage(mFilePanel, _("Files"));
        noteBook->AddPage(new RegistryPanel(noteBook), _("Registry"));
        mSettingsPanel = new SettingsPanel(noteBook);
        noteBook->AddPage(mSettingsPanel, _("Settings"));
        noteBook->AddPage(new LogPanel(noteBook), _("Log"));
        bSizer->Add(noteBook, 1, wxEXPAND);
    }
    {
        wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(new wxButton(owner, APPBOX_PACKER_MAINFRAME_PROCESS_BUTTON, _("Process")));
        sizer->Add(
            new wxButton(owner, APPBOX_PACKER_MAINFRAME_STARTUP_FILES_BUTTON, _("Startup Files")));
        bSizer->Add(sizer, 0, 0);
    }
    owner->SetSizer(bSizer);

    owner->CreateStatusBar();
    owner->Bind(wxEVT_MENU, &Data::OnProjectSave, this, wxID_SAVE);
    owner->Bind(wxEVT_MENU, &Data::OnProjectOpen, this, wxID_OPEN);
    owner->Bind(wxEVT_BUTTON, &Data::OnProcessButtonClick, this,
                APPBOX_PACKER_MAINFRAME_PROCESS_BUTTON);
    owner->Bind(wxEVT_BUTTON, &Data::OnStartupFilesButtonClick, this,
                APPBOX_PACKER_MAINFRAME_STARTUP_FILES_BUTTON);
}

void MainFrame::Data::OnProcessButtonClick(const wxCommandEvent&)
{
    SettingsPanel::Config settingsConfig = mSettingsPanel->Export();

    appbox::Meta meta;
    meta.settings.sandboxLocation = settingsConfig.sandboxPath;
    meta.settings.sandboxReset = settingsConfig.resetSandbox;

    ProcessDialog::Config processConfig;
    processConfig.compress = settingsConfig.compressLevel;
    processConfig.loaderPath = L"loader.exe";
    processConfig.outputPath = wxString::FromUTF8(settingsConfig.outputPath);
    processConfig.filesystem = mFilePanel->Export().fs;

    ProcessDialog dialog(mOwner, meta, processConfig);
    dialog.ShowModal();
}

void MainFrame::Data::OnStartupFilesButtonClick(const wxCommandEvent&)
{
    wxArrayString      choices = s_get_startup_choices(mFilePanel->Export());
    StartupFilesDialog dialog(mOwner, mStartupFiles, choices);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }
    mStartupFiles = dialog.GetResult();
}

void MainFrame::Data::OnProjectSave(const wxCommandEvent&)
{
    wxFileDialog saveDialog(mOwner, _("Save Project"), wxEmptyString, wxEmptyString,
                            "JSON Files (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (saveDialog.ShowModal() != wxID_OK)
    {
        return;
    }

    std::wstring       outPath = saveDialog.GetPath().ToStdWstring();
    appbox::FileHandle outFile(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS);
    if (outFile.get() == INVALID_HANDLE_VALUE)
    {
        wxMessageBox("Failed to create project file");
        return;
    }

    nlohmann::json project;
    project["filesystem"] = mFilePanel->Export();
    project["settings"] = mSettingsPanel->Export();
    project["startupFiles"] = mStartupFiles;
    std::string projectData = project.dump(2);
    appbox::WriteFileSized(outFile.get(), projectData.c_str(), projectData.size());
}

void MainFrame::Data::OnProjectOpen(const wxCommandEvent&)
{
    wxFileDialog openDialog(mOwner, _("Open Project"), wxEmptyString, wxEmptyString,
                            "JSON Files (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (openDialog.ShowModal() != wxID_OK)
    {
        return;
    }

    try
    {
        std::string           data = appbox::ReadFileAll(openDialog.GetPath().ToStdWstring());
        nlohmann::json        project = nlohmann::json::parse(data);
        SettingsPanel::Config settingsConfig = project["settings"];
        mSettingsPanel->Import(settingsConfig);
        FilePanel::Config fileConfig = project["filesystem"];
        mFilePanel->Import(fileConfig);
        mStartupFiles = project["startupFiles"];
    }
    catch (const std::runtime_error& e)
    {
        wxString msg = wxString::FromUTF8(e.what());
        wxMessageBox(L"Failed to open project file: " + msg);
    }
}

MainFrame::MainFrame() : wxFrame(nullptr, wxID_ANY, "AppBox Packer")
{
    m_data = new Data(this);
}

MainFrame::~MainFrame()
{
    delete m_data;
}
