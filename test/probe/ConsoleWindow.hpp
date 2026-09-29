#ifndef APPBOX_TEST_PROBE_CONSOLE_WINDOW_HPP
#define APPBOX_TEST_PROBE_CONSOLE_WINDOW_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolConsoleWindow
{
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
 * The loader is a GUI program without a console, so a console program it
 * starts gets a console window of its own. The end-to-end cases of the loader
 * use this probe to pin that the window of the probe process stays hidden
 * while the console itself is still there.
 */
extern Probe ProbeConsoleWindow;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_CONSOLE_WINDOW_HPP
