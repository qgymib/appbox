#ifndef APPBOX_LAUNCHER_UTILS_COMMANDLINEOPTIONS_HPP
#define APPBOX_LAUNCHER_UTILS_COMMANDLINEOPTIONS_HPP

#include "sandbox/utils/WinAPI.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox
{

struct CommandLineOptions
{
    CommandLineOptions();
    ~CommandLineOptions();

    /**
     * @brief Parse command line options
     * @return True if continue, false if exit.
     */
    bool ParseOptions();

    int                      wargc;               /* Command line argument count */
    LPWSTR*                  wargv;               /* Command line argument array */
    bool                     is_process_launcher; /* True if it is the process launcher of the launcher */
    bool                     hide_console;        /* True to start the target without a console window */
    std::wstring             config_dir;          /* Config file directory path */
    nlohmann::json           override_config;     /* Override config */
    std::vector<std::string> extra_args;          /* Extra arguments, encoding in UTF-8 */
    std::string              startup_trigger;     /* Trigger of the startup file to run, encoding in UTF-8 */
    bool                     has_startup_trigger; /* True if a startup trigger was given */
    bool                     shell;               /* True to run the shell of the host inside the sandbox */

    /**
     * @brief Level of the log files of the run, encoding in UTF-8.
     *
     * The level describes the whole run, so the launcher reports it and the
     * sandbox of every process it starts reports it as well. The value is
     * `off` while the run asks for no log file, which is what a run does unless
     * `--X-AppBox-LogLevel` names a level.
     */
    std::string log_level;

    /**
     * @brief Directory the log files of the run are written into.
     *
     * The value is the one `--X-AppBox-LogDir` carried, which is empty when the
     * option was not given. `SetupLogging()` replaces it by the resolved
     * absolute path of the directory and clears it when the run writes no log
     * file at all, so the sandbox of a process of the run receives a directory
     * only when there is a log to write.
     */
    std::wstring log_dir;
};

} // namespace appbox

#endif
