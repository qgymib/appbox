#include "WinAPI.h" /* Must be first include file */
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
appbox::test::ProtocolConsoleWindow::Rsp appbox::test::ProbeConsoleWindow_Entry(
    const appbox::test::ProtocolConsoleWindow::Req& /*request*/)
{
    const HWND hwnd = GetConsoleWindow();

    appbox::test::ProtocolConsoleWindow::Rsp rsp;
    rsp.attached = GetConsoleCP() != 0;
    rsp.visible = hwnd != nullptr && IsWindowVisible(hwnd) != FALSE;

    return rsp;
}
