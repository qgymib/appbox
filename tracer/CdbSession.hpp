#ifndef APPBOX_TRACER_CDBSESSION_HPP
#define APPBOX_TRACER_CDBSESSION_HPP

#include "tracer/ArmPlan.hpp"
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace appbox::tracer
{

/** How a trace run ended. */
enum class RunStatus
{
    Completed,   ///< The debugger exited on its own.
    Failed,      ///< The debugger could not be started or never reached a session.
    TimedOut,    ///< The hard time limit was reached.
    Stalled,     ///< The debugger stopped consuming the commands which were fed.
    Interrupted, ///< The run was interrupted by the user.
};

/** Result of one trace run. */
struct TraceResult
{
    std::vector<std::wstring> names;          ///< Every hit name, sorted and unique.
    std::vector<std::size_t> calls_per_process; ///< Observed calls per process, in session order.
    std::size_t processes = 0;                ///< Processes (debugger sessions) which were armed.
    std::size_t breakpoints = 0;              ///< Breakpoints armed in one process.
    std::size_t unexpected_stops = 0;         ///< Debugger stops the tracer had to continue.
    int debugger_exit_code = 0;               ///< Exit code of the debugger process.
    RunStatus status = RunStatus::Completed;  ///< How the run ended.
    std::wstring message;                     ///< Details of a run which did not complete.
};

/** Everything one trace run needs. */
struct TraceRequest
{
    std::filesystem::path debugger;         ///< Debugger to drive (cdb.exe).
    std::filesystem::path program;          ///< Program to run.
    std::vector<std::wstring> program_args; ///< Arguments of the program.
    std::vector<ArmGroup> plan;             ///< Breakpoints to arm in every process.
    unsigned timeout_seconds = 600;         ///< Hard limit of the whole run.
    unsigned stall_timeout_seconds = 30;    ///< Limit for consuming a batch which was fed.
    std::filesystem::path keep_raw_path;    ///< Optional file for the raw debugger output.
};

/**
 * @brief Run a program below the debugger and collect the functions it used.
 *
 * The debugger is driven as an interactive engine:
 *
 * - The breakpoint script is fed through the standard input of the debugger,
 *   because a command string on its command line is parsed unreliably and a
 *   script file is echoed, which would look like a hit.
 * - Every process gets its own script: a child process is a new debugger
 *   session which does not inherit the breakpoints, and the addresses have to
 *   be computed from the module base addresses of that session.
 * - The debugger prints one prompt per executed command, so
 *   `prompts - commands fed` is zero while the program runs and one while the
 *   debugger waits for input. That is how an unexpected stop (an exception, for
 *   example) is recognised and continued.
 *
 * Progress and diagnostics go to the standard error; the collected names are
 * returned. The function does not throw.
 *
 * @param[in] request What to run and what to arm.
 * @return The collected names and how the run ended.
 */
TraceResult RunTraceSession(const TraceRequest& request);

} // namespace appbox::tracer

#endif // APPBOX_TRACER_CDBSESSION_HPP
