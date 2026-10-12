#ifndef APPBOX_PACKER_CORE_STARTUP_SCREEN_HPP
#define APPBOX_PACKER_CORE_STARTUP_SCREEN_HPP

#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief The startup screen of a packer session.
 *
 * The screen is shown by the launcher of a standalone archive while it prepares
 * the sandbox: it presents the name of the packaged application, the startup
 * image of the session and the progress of the run, and it disappears as soon
 * as the processes of the run were started.
 *
 * The session stores two things only: whether the screen is shown at all and
 * the host path of the image the user selected. The image itself is read by
 * the pack run, which writes it into the archive; the name of the application
 * is not part of the session, because the launcher reads it from the file
 * properties the packer writes into itself (see the `Metadata` tab of the
 * Settings workspace).
 *
 * The structure is the counterpart of `LauncherStartupScreen` of
 * `launcher/Config.hpp`, which carries the resolved image entry and the flag
 * into the launcher.
 */
struct StartupScreen
{
    /**
     * @brief Whether the launcher of the archive shows the startup screen.
     *
     * A session which turns the screen off packs an archive whose launcher
     * starts the application without a window of its own.
     */
    bool enabled = true;

    /**
     * @brief Host path of the startup image, empty while none is set.
     *
     * The path is stored as it was selected on the machine which edited the
     * session; the file is read by the pack run, which stores it in the archive
     * under StartupImageEntryName().
     */
    std::wstring image;

    /**
     * @brief Whether the session describes a plain default.
     * @return true when the screen is shown and no image is set.
     */
    bool IsEmpty() const;
};

/**
 * @brief Get the image formats the startup screen accepts.
 *
 * The formats are the ones the launcher decodes through the imaging component
 * of Windows, which are the formats a user of a packaged application expects
 * for a logo. The list is the single place which spells them: the file dialog
 * of the Settings workspace and the validation of a pack run are derived from
 * it, so the two cannot drift apart.
 *
 * @return The extensions, lower case and with their leading dot, in the order
 *         the file dialog offers them.
 */
const std::vector<std::wstring>& StartupImageExtensions();

/**
 * @brief Get the lower case extension of a path, with its leading dot.
 *
 * Only the file name of the path is inspected: a dot of a directory name is
 * never taken for the extension of the file.
 *
 * @param[in] path Path to inspect.
 * @return The extension, e.g. `L".png"`; empty when the file name carries none.
 */
std::wstring StartupImageExtension(const std::wstring& path);

/**
 * @brief Whether a path names a startup image the launcher can decode.
 * @param[in] path Path to test.
 * @return true when the extension of the path is one of
 *         StartupImageExtensions().
 */
bool IsSupportedStartupImage(const std::wstring& path);

/**
 * @brief Whether a session sets a usable startup image.
 *
 * A path whose extension the launcher cannot decode does not count as an
 * image, so a session which names an unsupported file describes a screen
 * without an image instead of an archive whose launcher fails to decode one.
 *
 * @param[in] screen The startup screen of the session.
 * @return true when the session names an image the launcher can decode.
 */
bool HasStartupImage(const StartupScreen& screen);

/**
 * @brief Get the file name of the startup image inside the startup directory.
 *
 * The image is stored under the base name of the archive layout with the
 * extension of the selected file, so a session which selects `logo.PNG` packs
 * `splash.png`: the launcher configuration names one entry whatever format the
 * user picked.
 *
 * @param[in] host_path Host path of the selected image.
 * @return The file name, e.g. `L"splash.png"`; empty when the path carries no
 *         supported extension.
 */
std::wstring StartupImageFileName(const std::wstring& host_path);

/**
 * @brief Get the archive entry of the startup image, relative to the resource
 *        root.
 *
 * The name is the one the launcher configuration carries and the one the pack
 * run writes below `app`, e.g. `startup/splash.png`. It is pure ASCII, because
 * it is composed of the names of the archive layout and the extension of the
 * selected file.
 *
 * @param[in] host_path Host path of the selected image.
 * @return The entry name, e.g. `"startup/splash.png"`; empty when the path
 *         carries no supported extension.
 */
std::string StartupImageEntryName(const std::wstring& host_path);

/**
 * @brief Validate the startup screen of a session before a pack run.
 *
 * A session without an image is valid: the launcher then shows the name of the
 * application and the progress of the run only. A session which names an image
 * has to name a regular file the launcher can decode, because the pack run
 * reads the file while it writes the archive.
 *
 * @param[in] screen The startup screen of the session.
 * @param[out] error Description of the failure, empty when the screen is valid.
 * @return true when the screen can be packed.
 */
bool ValidateStartupScreen(const StartupScreen& screen, std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_STARTUP_SCREEN_HPP
