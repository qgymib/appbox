#ifndef APPBOX_LOADER_HPP
#define APPBOX_LOADER_HPP

#include <wx/wx.h>
#include <thread>
#include <filesystem>
#include <memory>
#include "widget/MainFrame.hpp"
#include "sandbox/Config.hpp"
#include "RemoteServer.hpp"
#include "utils/SandboxPaths.hpp"
#include "Config.hpp"

struct AppBoxLoaderRuntime
{
    typedef std::shared_ptr<AppBoxLoaderRuntime> Ptr;

    /**
     * @brief Build the runtime of one run.
     *
     * @param[in] log_level Level the run reports: `trace`, `debug`, `info`,
     *                      `warn`, `err`, `critical` or `off`. It is handed to
     *                      the sandbox of every process the run starts, so the
     *                      processes of a run report the same levels.
     */
    explicit AppBoxLoaderRuntime(const std::string& log_level);
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

    appbox::LoaderConfig                      loader_config;            /* Loader configuration */
    appbox::SandboxPaths                      sandbox_paths;            /* Layout of the sandbox */
    AppBoxLoaderRuntime::Ptr                  runtime;                  /* Runtime information */
    MainFrame*                                main_frame = nullptr;     /* Main frame */
    std::thread*                              working_thread = nullptr; /* Working thread */
    DWORD                                     exit_code = 0;            /* Exit code */
    std::vector<const appbox::LoaderStartup*> startups;                 /* Startup files to run */
    std::string                               startup_error;            /* Error which refuses the run */
    bool                                      shell = false; /* True to run the shell of the host in the sandbox */
    std::wstring                              shell_path; /* Shell of the host to run, empty outside the shell mode */
    std::vector<std::wstring>                 shell_args; /* Command the shell runs, empty for an interactive shell */
};
wxDECLARE_APP(AppBoxLoader);

/**
 * @brief Exit the application if no gui window shown.
 */
wxDECLARE_EVENT(APPBOX_EXIT_APPLICATION_IF_NO_GUI, wxCommandEvent);

#endif // APPBOX_LOADER_HPP
