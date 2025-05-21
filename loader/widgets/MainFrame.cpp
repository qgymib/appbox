#include <wx/wx.h>
#include <wx/aboutdlg.h>
#include <wx/notebook.h>
#include <spdlog/spdlog.h>
#include "LogPanel.hpp"
#include "MainFrame.hpp"

struct MainFrame::Data
{
    Data(MainFrame* owner);
    ~Data();

    MainFrame* owner;
};

static void s_on_about(wxCommandEvent&)
{
    wxAboutDialogInfo info;
    info.SetName("AppBox Loader");
    wxAboutBox(info);
}

MainFrame::Data::Data(MainFrame* owner)
{
    this->owner = owner;

    {
        wxMenuBar* menuBar = new wxMenuBar;
        {
            wxMenu* menuHelp = new wxMenu;
            menuHelp->Append(wxID_ABOUT);
            menuBar->Append(menuHelp, _("Help"));
        }
        owner->SetMenuBar(menuBar);
    }

    {
        wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);
        wxNotebook* noteBook = new wxNotebook(owner, wxID_ANY);
        noteBook->AddPage(new LogPanel(noteBook), _("Log"));
        bSizer->Add(noteBook, 1, wxEXPAND);
        owner->SetSizerAndFit(bSizer);
    }

    {
        owner->CreateStatusBar();
        owner->SetIcon(wxIcon("IDI_ICON1"));
        owner->SetSize(wxDefaultCoord, wxDefaultCoord, 800, 600, wxSIZE_AUTO);
    }

    owner->Bind(wxEVT_MENU, &s_on_about, wxID_ABOUT);
}

MainFrame::Data::~Data()
{
}

MainFrame::MainFrame() : wxFrame(nullptr, wxID_ANY, "AppBox Loader")
{
    m_data = new Data(this);
}

MainFrame::~MainFrame()
{
    delete m_data;
}
