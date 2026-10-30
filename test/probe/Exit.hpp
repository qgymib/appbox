#ifndef APPBOX_TEST_PROBE_EXIT_HPP
#define APPBOX_TEST_PROBE_EXIT_HPP

#include <cstdint>
#include <nlohmann/json.hpp>

namespace appbox::test
{

/**
 * @brief Message which ends a probe process with a code of its choice.
 *
 * The probe serves until it receives this request, so a case decides how long a
 * probe lives and how the program of a run leaves: the request replaces the
 * global exit code and the global hold time the probe executable used to take
 * from its command line. A case which needs a probe to stay alive simply does
 * not send it.
 *
 * The serve loop of the probe also ends when the pipe of the case closes, which
 * is what the end of the test executable does to every probe it owns: a probe
 * which outlived its case would keep the launcher of its run alive, and that
 * launcher holds the state of the sandbox the artifact of a failed run carries.
 * The exit code of a probe which lost its case is a failure, see
 * `test/probe/__init__.cpp`.
 */
struct ProtocolExit
{
    static constexpr const char* Method = "Exit";

    struct Req
    {
        std::int32_t code = 0; /* Exit code of the probe process. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, code)
    };

    struct Rsp
    {
        int _ = 0; /* Useless field. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, _)
    };
};

/**
 * @brief Ask the probe process to leave after it answered this request.
 * @param[in] request Request of the probe, which carries the exit code.
 * @return The answer of the probe.
 */
ProtocolExit::Rsp ProbeExit_Entry(const ProtocolExit::Req& request);

/**
 * @brief Whether a request asked this process to leave.
 * @return true when the exit request arrived.
 */
bool ExitRequested();

/**
 * @brief Exit code the request named.
 * @return The exit code, zero when no request arrived.
 */
std::int32_t ExitCode();

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_EXIT_HPP
