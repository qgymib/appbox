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
 * @brief Startup screen the launcher shows while it prepares the sandbox.
 *
 * The screen presents the name of the packaged application, the startup image
 * of the session and the progress of the run; it is destroyed as soon as the
 * processes of the run were started. A session which sets no image shows the
 * name and the progress only, and a session which turns the screen off shows
 * nothing at all.
 *
 * Neither the name nor the image is part of the document: the launcher reads the
 * name from the version resource the packer wrote into itself, so it always
 * matches the file properties the shell shows for the extracted program, and the
 * image is the RCDATA resource `IDR_APPBOX_STARTUP_IMAGE` of
 * `launcher/EmbeddedResourceIds.h`, which the packer embeds at pack time. A
 * payload without that resource shows the screen with the name and the progress
 * only.
 */
struct LauncherStartupScreen
{
    /**
     * @brief Whether the launcher shows the startup screen.
     *
     * The screen is shown unless the session turns it off, which keeps the
     * default behavior of a packed application: a user of the archive sees
     * that the program is being prepared instead of a launcher which seems to
     * do nothing.
     */
    bool screen = true;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LauncherStartupScreen, screen)
};

/**
 * @brief Configuration of the launcher of one packed application.
 *
 * The layout of the archive is a fixed convention (`common/SandboxLayout.hpp`):
 * `app` carries the read-only resources of the application, `data` carries the
 * state of the sandbox and is created at run time. Neither of them is named by
 * the configuration, so the document only describes what the launcher starts.
 *
 * The packer serializes the structure as UTF-8 JSON and writes it into the
 * launcher payload as the RCDATA resource `IDR_APPBOX_LAUNCHER_CONFIG` of
 * `launcher/EmbeddedResourceIds.h`; the launcher reads that resource at run time
 * instead of a file beside itself. The encoding is fixed: the document is written
 * without a byte order mark and non-ASCII characters are stored as their UTF-8
 * bytes.
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
     * @brief Startup screen the launcher shows, see LauncherStartupScreen.
     */
    LauncherStartupScreen startup;

    /**
     * @brief Startup files, in the order the launcher starts them.
     */
    std::vector<LauncherStartup> startups;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LauncherConfig, hide_console, startup, startups)
};

} // namespace appbox

#endif
