#ifndef APPBOX_LOADER_HPP
#define APPBOX_LOADER_HPP

#include <wx/wx.h>
#include <thread>
#include <filesystem>
#include <memory>
#include "widget/MainFrame.hpp"
#include "sandbox/Config.hpp"
#include "RemoteServer.hpp"
#include "Config.hpp"

struct AppBoxLoaderRuntime
{
    typedef std::shared_ptr<AppBoxLoaderRuntime> Ptr;

    AppBoxLoaderRuntime();
    ~AppBoxLoaderRuntime();

    appbox::SandboxConfig     inject_data; /* Inject data information */
    appbox::RemoteServer::Ptr pipe_server; /* Pipe server for remote communication */
};

struct AppBoxLoader : wxApp
{
    bool OnInit() override;
    int  OnExit() override;
    int  OnRun() override;
    void HandleEventExitApplicationNoGUI(wxCommandEvent&);

    appbox::LoaderConfig     loader_config;            /* Loader configuration */
    AppBoxLoaderRuntime::Ptr runtime;                  /* Runtime information */
    MainFrame*               main_frame = nullptr;     /* Main frame */
    std::thread*             working_thread = nullptr; /* Working thread */
    DWORD                    exit_code = 0;            /* Exit code */
};
wxDECLARE_APP(AppBoxLoader);

/**
 * @brief Exit the application if no gui window shown.
 */
wxDECLARE_EVENT(APPBOX_EXIT_APPLICATION_IF_NO_GUI, wxCommandEvent);

#endif // APPBOX_LOADER_HPP
