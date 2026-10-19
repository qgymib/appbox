#ifndef APPBOX_TEST_PROBE_REGCHILDPROCESS_HPP
#define APPBOX_TEST_PROBE_REGCHILDPROCESS_HPP

#include <CLI/CLI.hpp>
#include <string>
#include "utils/RegChildExitCode.hpp"

namespace appbox::test
{

/**
 * @brief Register the `regchild` subcommand of the probe executable.
 *
 * The subcommand is the worker a sandboxed probe starts as a child process of
 * its own: it opens a key of the sandbox registry, reads a value, writes the
 * values it was told to write and leaves with the bit mask of the observations
 * of `RegChildExitCode`. The worker never talks to the RPC server of the case,
 * so a child process whose registry writes are invisible to its parent still
 * reports what it did through its exit code.
 *
 * @param[in,out] app The CLI app of the probe executable.
 */
void RegChildProcessInit(CLI::App& app);

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGCHILDPROCESS_HPP
