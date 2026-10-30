#ifndef APPBOX_TEST_PROBE_LOADED_MODULES_HPP
#define APPBOX_TEST_PROBE_LOADED_MODULES_HPP

#include "WinAPI.h" /* Must be first include file */
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace appbox::test
{

struct ProtocolLoadedModules
{
    static constexpr const char* Method = "LoadedModules";

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
        std::vector<std::string> modules; /* Base names of the modules of the process. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, modules)
    };
};

/**
 * @brief Probe which reports the modules the probe process has loaded.
 *
 * The end-to-end cases of the launcher run the probe inside the sandbox, so the
 * list carries the modules of a sandboxed process: the injection modules the
 * launcher added and every module they depend on, which is what a case checks
 * that the sandbox adds no dependency of its own.
 *
 * @param[in] request Request of the probe.
 * @return The answer of the probe.
 */
ProtocolLoadedModules::Rsp ProbeLoadedModules_Entry(const ProtocolLoadedModules::Req& request);

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_LOADED_MODULES_HPP
