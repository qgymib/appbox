#ifndef APPBOX_TEST_PROBE_PARAM_CHECK_HPP
#define APPBOX_TEST_PROBE_PARAM_CHECK_HPP

#include <CLI/CLI.hpp>

namespace appbox::test
{

/**
 * @brief One observation the worker of a process parameter case reports.
 *
 * The worker leaves through its exit code, so every observation it makes is one
 * bit of that code: a case which expects the parameters of a child to name the
 * view of the sandbox asks for the bits of the observations it made.
 */
enum ParamCheckBit : unsigned long
{
    /** The image path of the process is the path the case expects. */
    ParamCheckImageMatched = 0x1,

    /** The working directory of the process is the path the case expects. */
    ParamCheckCurrentDirectoryMatched = 0x2,

    /** The module of the sandbox is loaded into the process. */
    ParamCheckSandboxLoaded = 0x4,
};

/**
 * @brief Every observation the worker reports.
 *
 * A code which carries a bit outside of this mask did not come from the worker,
 * which is how a case tells a report of the worker from the code of a process
 * which failed to start.
 */
inline constexpr unsigned long kParamCheckMask = 0x7;

/**
 * @brief Register the worker of the process parameter cases.
 * @param[in,out] app The CLI app of the probe.
 */
void ParamCheckInit(CLI::App& app);

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_PARAM_CHECK_HPP
