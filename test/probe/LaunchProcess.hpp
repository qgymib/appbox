#ifndef APPBOX_TEST_PROBE_LAUNCH_PROCESS_HPP
#define APPBOX_TEST_PROBE_LAUNCH_PROCESS_HPP

#include "WinAPI.h" /* Must be first include file */
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief The API a case creates the child process through.
 *
 * The two entry points reach the sandbox at different layers: `CreateProcessW`
 * runs the whole Win32 creation code and reaches `NtCreateUserProcess` at its
 * end, while `RtlCreateUserProcess` is the runtime of the system which builds
 * the parameters of a process itself. A case which names the second one pins
 * that a creation which never passes the Win32 entry point is sandboxed too.
 */
enum class LaunchApi
{
    CreateProcess,       /*!< CreateProcessW, the documented entry point. */
    RtlCreateUserProcess /*!< RtlCreateUserProcess, the runtime of the system. */
};

/* clang-format off */
NLOHMANN_JSON_SERIALIZE_ENUM(LaunchApi, {
    { LaunchApi::CreateProcess,       "CreateProcess"       },
    { LaunchApi::RtlCreateUserProcess, "RtlCreateUserProcess" },
})
/* clang-format on */

struct ProtocolLaunchProcess
{
    static constexpr const char* Method = "LaunchProcess";

    struct Req
    {
        std::string              FileName;                       /* Executable path encoding in UTF-8. */
        std::vector<std::string> Arguments;                      /* Arguments passed to the executable. */
        LaunchApi                Api = LaunchApi::CreateProcess; /* Entry point of the creation. */
        std::string              CurrentDirectory;               /* Working directory in UTF-8, empty to inherit. */

        /**
         * @brief Working directory of the probe itself, in UTF-8.
         *
         * A case sets it to make the probe hold a directory a child inherits,
         * which is how a case checks what a child makes of the directory of its
         * parent. Empty leaves the directory of the probe as it is.
         */
        std::string ParentCurrentDirectory;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, FileName, Arguments, Api, CurrentDirectory,
                                                    ParentCurrentDirectory)
    };

    struct Rsp
    {
        DWORD code = 0;      /* Error code of the creation, zero on success. */
        DWORD exit_code = 0; /* Exit code of the launched process. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, code, exit_code)
    };
};

/**
 * @brief Launch an executable from the sandbox view.
 *
 * @param[in] request Request of the probe.
 * @return The answer of the probe.
 */
ProtocolLaunchProcess::Rsp ProbeLaunchProcess_Entry(const ProtocolLaunchProcess::Req& request);

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_LAUNCH_PROCESS_HPP
