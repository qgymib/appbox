#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "StartupStarted.hpp"

static nlohmann::json ProbeStartupStarted_Entry(const nlohmann::json&)
{
    appbox::test::ProtocolStartupStarted::Rsp rsp;
    rsp.marker = appbox::test::StartupMarker();
    return rsp;
}

appbox::test::Probe appbox::test::ProbeStartupStarted("StartupStarted", ProbeStartupStarted_Entry);
