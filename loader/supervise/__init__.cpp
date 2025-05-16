#include <wx/wx.h>
#include <wx/event.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include "winapi.hpp"
#include "App.hpp"
#include "__init__.hpp"

class SuperviseService : public wxEvtHandler, public wxThreadHelper
{
public:
    SuperviseService();
    ~SuperviseService() override;

protected:
    wxThread::ExitCode Entry() override;

private:
    std::wstring self_path;
};

SuperviseService::SuperviseService()
{
    self_path = appbox::GetExePath();
    spdlog::info(L"Loader path: {}", self_path);

    if (CreateThread(wxTHREAD_JOINABLE) != wxTHREAD_NO_ERROR)
    {
        spdlog::error("Supervise service create failed");
        wxGetApp().QueueEvent(new wxCommandEvent(APPBOX_EXIT_APPLICATION_NO_GUI));
        return;
    }

    if (GetThread()->Run() != wxTHREAD_NO_ERROR)
    {
        spdlog::error("Supervise service run failed");
        wxGetApp().QueueEvent(new wxCommandEvent(APPBOX_EXIT_APPLICATION_NO_GUI));
        return;
    }
}

SuperviseService::~SuperviseService()
{
    if (GetThread())
    {
        GetThread()->Delete();
        if (GetThread()->IsRunning())
        {
            GetThread()->Wait();
        }
    }
}

wxThread::ExitCode SuperviseService::Entry()
{
    while (!GetThread()->TestDestroy())
    {
        wxMilliSleep(100);
    }

    return (wxThread::ExitCode)0;
}

static SuperviseService* G = nullptr;

void appbox::supervise::Init()
{
    G = new SuperviseService;
}

void appbox::supervise::Exit()
{
    delete G;
    G = nullptr;
}
