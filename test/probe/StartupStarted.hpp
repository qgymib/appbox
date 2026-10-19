#ifndef APPBOX_TEST_PROBE_STARTUP_STARTED_HPP
#define APPBOX_TEST_PROBE_STARTUP_STARTED_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace appbox::test
{

struct ProtocolStartupStarted
{
    struct Rsp
    {
        std::string marker; /* Marker of the startup file which started this process. */

        /**
         * @brief Process id of the process the launcher created.
         *
         * The id lets a case tell the processes of one run apart when the
         * launcher starts several startup files.
         */
        std::uint32_t pid = 0;

        /**
         * @brief Process id of the process which created this one.
         *
         * The launcher creates the startup file itself, so the id is the one of
         * the launcher of the run; a run which starts a process of its own in
         * between reports the id of that process instead.
         */
        std::uint32_t parent_pid = 0;

        /**
         * @brief Path of the image this process runs, in UTF-8.
         *
         * The sandbox starts a program from the host layer which holds it, so
         * the path names the layer and not the path of the view the
         * configuration spells.
         */
        std::string module_path;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, marker, pid, parent_pid, module_path)
    };
};

/**
 * @brief Probe which reports the startup file which started this process.
 *
 * The end-to-end cases of the launcher put a marker into the arguments of every
 * startup file, so the probe tells which of the startup files of a
 * configuration the launcher really started. The report carries the process
 * chain and the image of the process as well, which is what pins that the
 * launcher created the process itself and that it created it from the layer
 * which holds the image.
 */
extern Probe ProbeStartupStarted;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_STARTUP_STARTED_HPP
