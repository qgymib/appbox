#include <wx/wx.h>
#include <wx/cmdline.h>
#include <spdlog/spdlog.h>
#include "supervise/__init__.hpp"
#include "widgets/MainFrame.hpp"
#include "App.hpp"

struct LoaderApp::Data
{
    Data(LoaderApp* owner);

    LoaderApp* owner;
    bool       option_admin;
    MainFrame* main_frame;

    void HandleEventExitApplicationNoGUI(wxCommandEvent&);
};

wxDEFINE_EVENT(APPBOX_EXIT_APPLICATION_IF_NO_GUI, wxCommandEvent);

LoaderApp::Data::Data(LoaderApp* owner)
{
    this->owner = owner;
    option_admin = false;
    main_frame = nullptr;

    owner->Bind(APPBOX_EXIT_APPLICATION_IF_NO_GUI, &Data::HandleEventExitApplicationNoGUI, this);
}

bool LoaderApp::OnInit()
{
    m_data = new Data(this);

    /* The command line arguments parser here. */
    if (!wxApp::OnInit())
    {
        return false;
    }

    m_data->main_frame = new MainFrame;
    m_data->main_frame->Show(m_data->option_admin);

    appbox::supervise::Init();
    return true;
}

int LoaderApp::OnExit()
{
    appbox::supervise::Exit();
    delete m_data;
    return 0;
}

void LoaderApp::OnInitCmdLine(wxCmdLineParser& parser)
{
    /* clang-format off */
    static const wxCmdLineEntryDesc options[] = {
        {
            wxCMD_LINE_SWITCH,
            nullptr,
            "X-AppBox-Admin",
            "Enable management interface",
            wxCMD_LINE_VAL_NONE,
            wxCMD_LINE_PARAM_OPTIONAL,
        },
        wxCMD_LINE_DESC_END,
    };
    /* clang-format on */
    parser.SetDesc(options);
    parser.SetSwitchChars("-");

    wxApp::OnInitCmdLine(parser);
}

bool LoaderApp::OnCmdLineParsed(wxCmdLineParser& parser)
{
    m_data->option_admin = parser.Found("X-AppBox-Admin");
    return wxApp::OnCmdLineParsed(parser);
}

void LoaderApp::Data::HandleEventExitApplicationNoGUI(wxCommandEvent&)
{
    spdlog::info("Try to exit application");
    if (!main_frame->IsShown())
    {
        main_frame->Close();
    }
}

wxIMPLEMENT_APP(LoaderApp); // NOLINT
