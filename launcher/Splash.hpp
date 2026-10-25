#ifndef APPBOX_LAUNCHER_SPLASH_HPP
#define APPBOX_LAUNCHER_SPLASH_HPP

#include <windows.h>
#include <string>
#include <string_view>
#include "utils/StartupImage.hpp"
#include "utils/StartupLayout.hpp"

namespace appbox
{

/**
 * @brief The startup screen of the launcher.
 *
 * The screen is a borderless window which presents the name of the packaged
 * application, its startup image and the progress of the run. It is created
 * before the heavy part of a run - loading the patch packages, mounting the
 * sandbox and creating the processes - and destroyed as soon as the processes
 * were started, so a user sees that the program is being prepared instead of a
 * launcher which seems to do nothing.
 *
 * The launcher owns no message loop, so the screen drives its own: Pump()
 * drains the message queue of the thread, which lets the timer animate the
 * progress bar and the window repaint itself. A caller which reports its
 * progress through SetStage() therefore keeps the screen alive while it works.
 *
 * A screen which is turned off, whose window cannot be created (a run without
 * an interactive window station) or whose image resource cannot be decoded is a
 * no-op: every method returns immediately, so a failure of the screen never
 * fails the run.
 */
class SplashScreen
{
public:
    /**
     * @brief Create the screen and show it.
     *
     * The screen is created without activating itself, so it never takes the
     * focus from the program the run starts. A screen which cannot be created
     * is closed: every method of such a screen is a no-op.
     *
     * The screen owns its window, so the caller keeps it in place while the
     * run works: the class is not copyable and not movable.
     *
     * @param[in] enabled Whether the session shows a startup screen at all.
     * @param[in] display_name Name of the packaged application.
     * @param[in] image_data Bytes of the startup image resource, empty while
     *                       the launcher carries none.
     */
    SplashScreen(bool enabled, const std::wstring& display_name, std::string_view image_data);

    ~SplashScreen();

    SplashScreen(const SplashScreen&) = delete;
    SplashScreen& operator=(const SplashScreen&) = delete;
    SplashScreen(SplashScreen&&) = delete;
    SplashScreen& operator=(SplashScreen&&) = delete;

    /**
     * @brief Report the progress of the run.
     *
     * The bar moves towards the reported value instead of jumping, so a run
     * which reports the boundaries of its stages still shows a bar which
     * grows. The call pumps the message queue, which is what keeps the screen
     * responsive while the caller works.
     *
     * @param[in] stage Stage the run is in.
     * @param[in] fraction Progress inside the stage, `0` at its beginning and
     *                     `1` at its end.
     */
    void SetStage(StartupStage stage, double fraction);

    /**
     * @brief Drain the message queue of the thread.
     *
     * The call lets the window paint itself and lets the timer advance the
     * progress bar; a caller which works for a long time without reporting
     * progress calls it to keep the screen responsive.
     */
    void Pump();

    /**
     * @brief Destroy the screen.
     *
     * The call is idempotent, so every path of a run can close the screen
     * without asking whether it is open.
     */
    void Close();

    /**
     * @brief Whether the screen is shown.
     * @return true while the window of the screen exists.
     */
    bool IsOpen() const
    {
        return window_ != nullptr;
    }

    /**
     * @brief Get the window of the screen.
     * @return The window handle, null while the screen is closed.
     */
    HWND Window() const
    {
        return window_;
    }

    /**
     * @brief Get the layout the screen draws with.
     * @return The layout of the screen.
     */
    const StartupLayout& Layout() const
    {
        return layout_;
    }

private:
    /**
     * @brief Create the window of the screen.
     * @return true when the window exists afterwards.
     */
    bool CreateWindowHandle();

    /**
     * @brief Create the resources the screen draws with.
     *
     * The brushes and the buffer of the window are created once, so a repaint
     * only draws instead of building the resources again.
     */
    void CreateResources();

    /**
     * @brief Release the resources the screen draws with.
     */
    void ReleaseResources();

    /**
     * @brief Draw the screen into the buffer and copy it to the window.
     */
    void Paint();

    /**
     * @brief Move the bar towards the progress the run reported.
     */
    void AdvanceProgress();

    /**
     * @brief Handle a message of the window of the screen.
     * @param[in] message Message to handle.
     * @param[in] wparam First parameter of the message.
     * @param[in] lparam Second parameter of the message.
     * @return The result of the message.
     */
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    /**
     * @brief The window procedure of the screen.
     * @param[in] window Window which received the message.
     * @param[in] message Message to handle.
     * @param[in] wparam First parameter of the message.
     * @param[in] lparam Second parameter of the message.
     * @return The result of the message.
     */
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HWND window_ = nullptr; ///< The window of the screen, null while it is closed.

    std::wstring  name_;   ///< Name of the packaged application.
    StartupImage  image_;  ///< Startup image, invalid while the session sets none.
    StartupLayout layout_; ///< Layout the screen draws with.

    HFONT  font_ = nullptr;       ///< Font of the name of the application.
    HBRUSH background_ = nullptr; ///< Brush of the card the screen draws.
    HBRUSH border_ = nullptr;     ///< Brush of the border of the card.
    HBRUSH track_ = nullptr;      ///< Brush of the empty part of the progress bar.
    HBRUSH fill_ = nullptr;       ///< Brush of the filled part of the progress bar.

    HDC     buffer_dc_ = nullptr;         ///< Device context the screen is composed in.
    HBITMAP buffer_bitmap_ = nullptr;     ///< Bitmap of the buffer.
    HDC     image_dc_ = nullptr;          ///< Device context which holds the startup image.
    HGDIOBJ old_buffer_bitmap_ = nullptr; ///< Bitmap the buffer device context replaced.

    double target_ = 0.0; ///< Progress the run reported, in `[0, 1]`.
    double shown_ = 0.0;  ///< Progress the bar shows, in `[0, 1]`.
};

} // namespace appbox

#endif // APPBOX_LAUNCHER_SPLASH_HPP
