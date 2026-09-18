#ifndef APPBOX_TEST_PROBE_LAUNCH_PROCESS_HPP
#define APPBOX_TEST_PROBE_LAUNCH_PROCESS_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace appbox::test
{

struct ProtocolLaunchProcess
{

    struct Req
    {
        std::string              FileName;  /* Executable path encoding in UTF-8. */
        std::vector<std::string> Arguments; /* Arguments passed to the executable. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, FileName, Arguments)
    };

    struct Rsp
    {
        DWORD code = 0;      /* CreateProcessW error code, zero on success. */
        DWORD exit_code = 0; /* Exit code of the launched process. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, code, exit_code)
    };
};

/**
 * @brief CreateProcessW probe launching an executable from the sandbox view.
 */
extern Probe ProbeLaunchProcess;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_LAUNCH_PROCESS_HPP
