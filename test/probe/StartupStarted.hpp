#ifndef APPBOX_TEST_PROBE_STARTUP_STARTED_HPP
#define APPBOX_TEST_PROBE_STARTUP_STARTED_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolStartupStarted
{
    struct Rsp
    {
        std::string marker; /* Marker of the startup file which started this process. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, marker)
    };
};

/**
 * @brief Probe which reports the startup file which started this process.
 *
 * The end-to-end cases of the loader put a marker into the arguments of every
 * startup file, so the probe tells which of the startup files of a
 * configuration the loader really started.
 */
extern Probe ProbeStartupStarted;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_STARTUP_STARTED_HPP
