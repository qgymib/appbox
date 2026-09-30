#ifndef APPBOX_LAUNCHER_HPP
#define APPBOX_LAUNCHER_HPP

#include <wx/wx.h>
#include <thread>
#include <filesystem>
#include <memory>
#include "widget/MainFrame.hpp"
#include "sandbox/Config.hpp"
#include "RemoteServer.hpp"
#include "utils/SandboxPaths.hpp"
#include "Config.hpp"

struct AppBoxLauncherRuntime
{
    typedef std::shared_ptr<AppBoxLauncherRuntime> Ptr;

    /**
     * @brief Build the runtime of one run.
     *
     * @param[in] log_level Level the run reports: `trace`, `debug`, `info`,
     *                      `warn`, `err`, `critical` or `off`. It is handed to
     *                      the sandbox of every process the run starts, so the
     *                      processes of a run report the same levels.
     */
    explicit AppBoxLauncherRuntime(const std::string& log_level);
    ~AppBoxLauncherRuntime();

    appbox::SandboxConfig     inject_data; /* Inject data information */
    appbox::RemoteServer::Ptr pipe_server; /* Pipe server for remote communication */
};

struct AppBoxLauncher : wxApp
{
    bool OnInit() override;
    int  OnExit() override;
    int  OnRun() override;
    void HandleEventExitApplicationNoGUI(wxCommandEvent&);

    appbox::LauncherConfig                      launcher_config;          /* Launcher configuration */
    appbox::SandboxPaths                        sandbox_paths;            /* Layout of the sandbox */
    AppBoxLauncherRuntime::Ptr                  runtime;                  /* Runtime information */
    MainFrame*                                  main_frame = nullptr;     /* Main frame */
    std::thread*                                working_thread = nullptr; /* Working thread */
    DWORD                                       exit_code = 0;            /* Exit code */
    std::vector<const appbox::LauncherStartup*> startups;                 /* Startup files to run */
    std::string                                 startup_error;            /* Error which refuses the run */
    bool                                        shell = false; /* True to run the shell of the host in the sandbox */
    std::wstring                                shell_path; /* Shell of the host to run, empty outside the shell mode */
    std::vector<std::wstring>                   shell_args; /* Command the shell runs, empty for an interactive shell */
};
wxDECLARE_APP(AppBoxLauncher);

/**
 * @brief Exit the application if no gui window shown.
 */
wxDECLARE_EVENT(APPBOX_EXIT_APPLICATION_IF_NO_GUI, wxCommandEvent);

#endif // APPBOX_LAUNCHER_HPP
