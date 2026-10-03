#ifndef APPBOX_LAUNCHER_CONFIG_HPP
#define APPBOX_LAUNCHER_CONFIG_HPP

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief One executable the launcher starts.
 *
 * A startup file with `auto_start` set is started as soon as the launcher runs.
 * A startup file without the flag is started only when the launcher is asked for
 * its `trigger` through the `--X-AppBox-Startup` option.
 */
struct LauncherStartup
{
    /**
     * @brief Trigger which selects the startup file.
     */
    std::string trigger;

    /**
     * @brief Whether the launcher starts the file without being asked to.
     */
    bool auto_start = true;

    /**
     * @brief Executable path.
     */
    std::string executable;

    /**
     * @brief Executable arguments.
     */
    std::vector<std::string> arguments;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LauncherStartup, trigger, auto_start, executable, arguments)
};

/**
 * @brief Configuration of the launcher of one packed application.
 *
 * The layout of the archive is a fixed convention (`common/SandboxLayout.hpp`):
 * `app` carries the read-only resources of the application, `data` carries the
 * state of the sandbox and is created at run time. Neither of them is named by
 * the configuration, so the file only describes what the launcher starts.
 */
struct LauncherConfig
{
    /**
     * @brief Start the startup files without a console window.
     *
     * The launcher is a GUI program without a console, so a console program it
     * starts gets a console window of its own. With this flag the window is
     * created hidden instead of being shown, which is what an unattended run
     * needs: the program keeps a console, and therefore its standard streams,
     * but nothing pops up on the desktop. The flag has no effect on a GUI
     * program, which never has a console window.
     */
    bool hide_console = false;

    /**
     * @brief Startup files, in the order the launcher starts them.
     */
    std::vector<LauncherStartup> startups;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LauncherConfig, hide_console, startups)
};

} // namespace appbox

#endif
