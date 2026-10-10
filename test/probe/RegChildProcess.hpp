#ifndef APPBOX_TEST_PROBE_REGCHILDPROCESS_HPP
#define APPBOX_TEST_PROBE_REGCHILDPROCESS_HPP

#include <CLI/CLI.hpp>
#include <string>

namespace appbox::test
{

/**
 * @brief Exit code bits of the registry worker process.
 *
 * The worker runs inside the sandbox and is started by another sandboxed
 * process, so the registry state it observes is not necessarily visible to its
 * parent. The exit code is therefore the channel which reports the work of the
 * worker back to the caller, and it is a bit mask instead of a single value, so
 * a partial success stays readable:
 *
 * * `RegChildOpenedKey` — the key of the case could be opened with the read and
 *   the write access.
 * * `RegChildReadValue` — the value named by `--read` exists and was read.
 * * `RegChildValueMatched` — the value named by `--read` carries the text of
 *   `--expect`.
 * * `RegChildWroteValues` — every value of `--set` was written.
 *
 * A code which carries none of the bits reports the failure of the first step
 * which did not succeed, and a code outside this mask (a `STATUS_*` value, for
 * example `0xC0000142`) means that the worker never reached its own code, for
 * example because the sandbox of the process failed to attach.
 */
enum RegChildExitCode : int
{
    RegChildOpenedKey = 1 << 0,
    RegChildReadValue = 1 << 1,
    RegChildValueMatched = 1 << 2,
    RegChildWroteValues = 1 << 3,
};

/** Mask of every bit the worker sets in its exit code. */
constexpr int kRegChildExitCodeMask =
    RegChildOpenedKey | RegChildReadValue | RegChildValueMatched | RegChildWroteValues;

/**
 * @brief Describe the exit code of the worker for a test message.
 *
 * @param[in] code The exit code of the worker process.
 * @return A readable description of the bits, or of the reason why the code is
 *         not one of the worker.
 */
std::string DescribeRegChildExitCode(unsigned long code);

/**
 * @brief Register the `regchild` subcommand of the test executable.
 *
 * The subcommand is the worker a sandboxed probe starts as a child process of
 * its own: it opens a key of the sandbox registry, reads a value, writes the
 * values it was told to write and leaves with the bit mask of the observations
 * above. The worker never talks to the RPC server of the case, so a child
 * process whose registry writes are invisible to its parent still reports what
 * it did through its exit code.
 *
 * @param[in,out] app The CLI app of the test executable.
 */
void RegChildProcessInit(CLI::App& app);

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGCHILDPROCESS_HPP
