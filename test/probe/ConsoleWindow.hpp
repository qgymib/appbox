#ifndef APPBOX_TEST_PROBE_CONSOLE_WINDOW_HPP
#define APPBOX_TEST_PROBE_CONSOLE_WINDOW_HPP

#include "WinAPI.h" /* Must be first include file */
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolConsoleWindow
{
    static constexpr const char* Method = "ConsoleWindow";

    /**
     * @brief Request of the probe.
     *
     * The probe takes no argument, so the request carries a placeholder member:
     * the parameters of a JSON-RPC request are an object.
     */
    struct Req
    {
        int _ = 0; /* Useless field. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, _)
    };

    struct Rsp
    {
        bool attached = false; /* The process owns a console, so its standard streams stay usable. */
        bool visible = false;  /* The console window of the process is visible on the desktop. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, attached, visible)
    };
};

/**
 * @brief Probe which reports the console window of the probe process.
 *
 * The launcher is a GUI program without a console, so a console program it
 * starts gets a console window of its own. The end-to-end cases of the launcher
 * use this probe to pin that the window of the probe process stays hidden
 * while the console itself is still there.
 *
 * @param[in] request Request of the probe.
 * @return The answer of the probe.
 */
ProtocolConsoleWindow::Rsp ProbeConsoleWindow_Entry(const ProtocolConsoleWindow::Req& request);

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_CONSOLE_WINDOW_HPP
