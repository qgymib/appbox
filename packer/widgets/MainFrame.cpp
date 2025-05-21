#include <wx/wx.h>
#include <wx/filedlg.h>
#include <wx/notebook.h>
#include <wx/wfstream.h>
#include "widgets/FilePanel.hpp"
#include "widgets/RegistryPanel.hpp"
#include "widgets/SettingsPanel.hpp"
#include "widgets/ProcessDialog.hpp"
#include "MainFrame.hpp"

#include "utils/file.hpp"

struct MainFrame::Data
{
    Data(MainFrame* owner);
    void OnProcessButtonClick(const wxCommandEvent&);
    void OnProjectSave(const wxCommandEvent&);

    MainFrame*            mOwner;
    wxButton*             mProcessButton;
    FilePanel*            mFilePanel;
    SettingsPanel*        mSettingsPanel;
    appbox::Meta          mMeta;
    ProcessDialog::Config mConfig;
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

    wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);
    {
        wxNotebook* noteBook = new wxNotebook(owner, wxID_ANY);
        mFilePanel = new FilePanel(noteBook);
        noteBook->AddPage(mFilePanel, _("Files"));
        noteBook->AddPage(new RegistryPanel(noteBook), _("Registry"));
        mSettingsPanel = new SettingsPanel(noteBook);
        noteBook->AddPage(mSettingsPanel, _("Settings"));
        bSizer->Add(noteBook, 1, wxEXPAND);
    }
    {
        wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
        mProcessButton = new wxButton(owner, wxID_ANY, _("Process"));
        sizer->AddStretchSpacer(0);
        sizer->Add(mProcessButton, 1, wxEXPAND);
        bSizer->Add(sizer, 0, 0);
    }
    owner->SetSizerAndFit(bSizer);

    owner->CreateStatusBar();
    owner->SetSize(wxDefaultCoord, wxDefaultCoord, 800, 600, wxSIZE_AUTO);
    owner->Bind(wxEVT_MENU, &Data::OnProjectSave, this, wxID_SAVE);
    mProcessButton->Bind(wxEVT_BUTTON, &Data::OnProcessButtonClick, this);
}

void MainFrame::Data::OnProcessButtonClick(const wxCommandEvent&)
{
    mConfig.loaderPath = L"loader.exe";
    mConfig.outputPath = mSettingsPanel->Export().outputPath;
    mConfig.filesystem = mFilePanel->Export();
    ProcessDialog dialog(mOwner, mMeta, mConfig);
    dialog.ShowModal();
}

void MainFrame::Data::OnProjectSave(const wxCommandEvent&)
{
    wxFileDialog saveDialog(mOwner, _("Save Project"), wxEmptyString, wxEmptyString,
                            "JSON Files (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (saveDialog.ShowModal() == wxID_CANCEL)
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
    project["meta"] = mMeta;
    project["config"] = mConfig;
    std::string projectData = project.dump();
    appbox::WriteFileSized(outFile.get(), projectData.c_str(), projectData.size());
}

MainFrame::MainFrame() : wxFrame(nullptr, wxID_ANY, "AppBox Packer")
{
    m_data = new Data(this);
}

MainFrame::~MainFrame()
{
    delete m_data;
}
