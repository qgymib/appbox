#ifndef APPBOX_PACKER_CORE_TRACER_MODEL_HPP
#define APPBOX_PACKER_CORE_TRACER_MODEL_HPP

#include "tracer/CdbSession.hpp"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace appbox
{

/** Hard limit of one run of the workspace, in seconds. */
inline constexpr unsigned kTracerRunTimeoutSeconds = 600;

/** Seconds the debugger may stay stopped before a run of the workspace is aborted. */
inline constexpr unsigned kTracerStallTimeoutSeconds = 30;

/**
 * @brief Kind of the function list the tracer workspace shows.
 */
enum class TracerView
{
    Scope = 0,  ///< Lowest level entry points of the isolation domains (the built in table).
    AllExports, ///< Every executable export parsed from the traced modules.
};

/**
 * @brief One row of the tracer list.
 */
struct TracerEntry
{
    std::wstring name;         ///< `module!function`.
    bool         used = false; ///< Whether the last run reported the function.
};

/**
 * @brief Name of a view as it is written into the exported JSON.
 * @param[in] view View to name.
 * @return `scope` or `all-exports`.
 */
std::string TracerViewKey(TracerView view);

/**
 * @brief Name of a view as the workspace shows it in its box.
 * @param[in] view View to name.
 * @return `Isolation entry points` or `All exports`.
 */
const char* TracerViewDisplayName(TracerView view);

/**
 * @brief Build the rows of one view from the traced modules of a directory.
 *
 * The rows are the names a run of the same view can report, because both are built from the
 * same breakpoint plan (`BuildArmPlan`): the plan resolves a forwarded export to the address
 * of its implementation and groups every name of that address, so the `Zw` alias of an NT
 * entry point (`ntdll!ZwClose`) is a row of its own and the rows of the `Scope` view are the
 * names of the built in table (`ScopeTable()`) plus those aliases minus the exports which can
 * not carry a breakpoint. A module which can not be read is skipped, exactly as in a run.
 *
 * The rows are sorted ASCII ascending by name and are all unused.
 *
 * @param[in] view View to build.
 * @param[in] module_directory Directory which holds the system modules of the target.
 * @param[out] entries Rows of the view; cleared on failure.
 * @param[out] error Error text, empty on success.
 * @return Whether the view could be built.
 */
bool BuildTracerView(TracerView view, const std::filesystem::path& module_directory, std::vector<TracerEntry>& entries,
                     std::string& error);

/**
 * @brief Mark the rows a run reported as used.
 *
 * @param[in,out] entries Rows to mark.
 * @param[in] used `module!function` names the run collected.
 */
void MarkTracerEntries(std::vector<TracerEntry>& entries, const std::vector<std::wstring>& used);

/**
 * @brief Order the rows for the workspace.
 *
 * The used rows come first and the unused ones follow, each group sorted ASCII ascending by
 * name. Both groups are sorted before they are separated, so one call is enough.
 *
 * @param[in,out] entries Rows to order.
 */
void OrderTracerEntries(std::vector<TracerEntry>& entries);

/** Result of one run of the workspace. */
struct TracerRunOutcome
{
    std::wstring                error;  ///< Set when the run could not be prepared or started.
    appbox::tracer::TraceResult result; ///< Names which were used and how the run ended.
};

/**
 * @brief One line description of a finished run, for the status line of the workspace.
 *
 * The wording is `label: value` (instead of a sentence), so it reads the same for one and for
 * several processes: `processes: 2, breakpoints: 120, functions used: 46`.
 *
 * @param[in] result Result of the run.
 * @return `processes: <n>, breakpoints: <n>, functions used: <n>`, or
 *         `the run did not complete: <message>`.
 */
std::wstring TracerRunSummary(const appbox::tracer::TraceResult& result);

/**
 * @brief Run one trace of the workspace.
 *
 * The debugger is resolved with the default search of the library (`ResolveCdb`), the plan is
 * built for the view and `RunTraceSession` drives the debugger. The function blocks until the
 * run ended and must be called from a worker thread; `RequestTraceInterrupt()` stops it.
 *
 * @param[in] target Resolved path of the program to run.
 * @param[in] program_args Arguments of the program.
 * @param[in] view View which decides the armed plan.
 * @param[in] module_directory Directory which holds the system modules of the target.
 * @param[in] progress Reports one progress line of the run; may be empty.
 * @return The result, or a non empty error when the run could not be prepared.
 */
TracerRunOutcome RunTracerSession(const std::filesystem::path& target, const std::vector<std::wstring>& program_args,
                                  TracerView view, const std::filesystem::path& module_directory,
                                  const std::function<void(const std::wstring&)>& progress);

/**
 * @brief Write the result of a run as the JSON export of the workspace.
 *
 * The document holds the program, the view, the result of the run, the number of processes
 * and breakpoints and the functions the run used (ASCII ascending, the used rows of the
 * view). Unused functions are not exported.
 *
 * @param[in] path File to write.
 * @param[in] program Program which was traced.
 * @param[in] view View the run used.
 * @param[in] result Result of the run.
 * @param[in] entries Rows of the view, in display order.
 * @param[out] error Error text, empty when the file was written.
 * @return Whether the file was written.
 */
bool SaveTracerExport(const std::wstring& path, const std::wstring& program, TracerView view,
                      const appbox::tracer::TraceResult& result, const std::vector<TracerEntry>& entries,
                      std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_TRACER_MODEL_HPP
