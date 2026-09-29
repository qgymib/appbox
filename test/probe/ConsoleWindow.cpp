#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "ConsoleWindow.hpp"

/**
 * @brief Report the console of the probe process.
 *
 * A process which owns no console reports no attached console; a console which
 * was created without a window reports an attached console whose window is not
 * visible.
 *
 * @return The response of the probe.
 */
static nlohmann::json ProbeConsoleWindow_Entry(const nlohmann::json&)
{
    const HWND hwnd = GetConsoleWindow();

    appbox::test::ProtocolConsoleWindow::Rsp rsp;
    rsp.attached = GetConsoleCP() != 0;
    rsp.visible = hwnd != nullptr && IsWindowVisible(hwnd) != FALSE;

    return rsp;
}

appbox::test::Probe appbox::test::ProbeConsoleWindow("ConsoleWindow", ProbeConsoleWindow_Entry);
