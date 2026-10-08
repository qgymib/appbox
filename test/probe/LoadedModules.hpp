#ifndef APPBOX_TEST_PROBE_LOADED_MODULES_HPP
#define APPBOX_TEST_PROBE_LOADED_MODULES_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace appbox::test
{

struct ProtocolLoadedModules
{
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
 */
extern Probe ProbeLoadedModules;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_LOADED_MODULES_HPP
