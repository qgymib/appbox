#include <wx/wx.h>
#include <wx/filedlg.h>
#include <wx/notebook.h>
#include <wx/wfstream.h>
#include "widgets/FilePanel.hpp"
#include "widgets/RegistryPanel.hpp"
#include "widgets/SettingsPanel.hpp"
#include "widgets/ProcessDialog.hpp"
#include "LogPanel.hpp"
#include "utils/file.hpp"
#include "utils/wstring.hpp"
#include "MainFrame.hpp"

struct MainFrame::Data
{
    Data(MainFrame* owner);
    void OnProcessButtonClick(const wxCommandEvent&);
    void OnProjectSave(const wxCommandEvent&);
    void OnProjectOpen(const wxCommandEvent&);

    MainFrame*     mOwner;
    wxButton*      mProcessButton;
    FilePanel*     mFilePanel;
    SettingsPanel* mSettingsPanel;
};

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
        mProcessButton = new wxButton(owner, wxID_ANY, _("Process"));
        sizer->AddStretchSpacer(0);
        sizer->Add(mProcessButton, 1, wxEXPAND);
        bSizer->Add(sizer, 0, 0);
    }
    owner->SetSizer(bSizer);

    owner->CreateStatusBar();
    owner->Bind(wxEVT_MENU, &Data::OnProjectSave, this, wxID_SAVE);
    owner->Bind(wxEVT_MENU, &Data::OnProjectOpen, this, wxID_OPEN);
    mProcessButton->Bind(wxEVT_BUTTON, &Data::OnProcessButtonClick, this);
}

void MainFrame::Data::OnProcessButtonClick(const wxCommandEvent&)
{
    SettingsPanel::Config settingsConfig = mSettingsPanel->Export();

    appbox::Meta meta;
    meta.settings.SandboxLocation = settingsConfig.sandboxPath;

    ProcessDialog::Config processConfig;
    processConfig.compress = settingsConfig.compressLevel;
    processConfig.loaderPath = L"loader.exe";
    processConfig.outputPath = wxString::FromUTF8(settingsConfig.outputPath);
    processConfig.filesystem = mFilePanel->Export().fs;

    ProcessDialog dialog(mOwner, meta, processConfig);
    dialog.ShowModal();
}

void MainFrame::Data::OnProjectSave(const wxCommandEvent&)
{
    wxFileDialog saveDialog(mOwner, _("Save Project"), wxEmptyString, wxEmptyString,
                            "JSON Files (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (saveDialog.ShowModal() != wxID_OK)
    {
        return;
    }

    std::wstring          outPath = saveDialog.GetPath().ToStdWstring();
    std::shared_ptr<void> outFile(CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr),
                                  CloseHandle);
    if (outFile.get() == INVALID_HANDLE_VALUE)
    {
        wxMessageBox("Failed to create project file");
        return;
    }

    nlohmann::json project;
    project["filesystem"] = mFilePanel->Export();
    project["settings"] = mSettingsPanel->Export();
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
