#ifndef APPBOX_TRACER_TRACEREPORT_HPP
#define APPBOX_TRACER_TRACEREPORT_HPP

#include "tracer/ArmPlan.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace appbox::tracer
{

/**
 * @brief Format the listing which `--list-scope` prints.
 *
 * The listing shows what a run would arm: the scope, then every breakpoint
 * address with all names which resolve to it, grouped by the module which holds
 * the implementation. It is the view used to review and tune the scope
 * patterns, so it may be more detailed than the final report.
 *
 * @param[in] plan Breakpoint plan to describe.
 * @param[in] scope Text which names the scope, e.g. `file, registry, network`.
 * @param[in] with_categories Whether every name is annotated with its categories.
 * @return The listing text, terminated by a newline.
 */
std::wstring FormatScope(const std::vector<ArmGroup>& plan, const std::wstring& scope,
                         bool with_categories);

/** Everything the header of the final report shows. */
struct TraceReportHeader
{
    std::wstring program;         ///< Program which was traced.
    std::wstring debugger;        ///< Debugger which was used.
    std::wstring scope;           ///< Scope of the run, e.g. `file, registry, network`.
    std::size_t processes = 0;    ///< Processes which were traced.
    std::size_t breakpoints = 0;  ///< Breakpoints which were armed per process.
    std::wstring status;          ///< `completed` or `aborted: <reason>`.
};

/**
 * @brief Format the final report.
 *
 * The report lists the functions which were used, grouped by the module which
 * exports them, sorted and without duplicates. A name is reported for every
 * alias of the address which was entered, because the same function can be
 * reachable through several names (`ntdll!NtClose` and `ntdll!ZwClose`, or
 * `kernel32!HeapAlloc` and `kernelbase!HeapAlloc`).
 *
 * @param[in] header Facts about the run.
 * @param[in] names `module!function` names which were used.
 * @param[in] with_categories Whether every name is annotated with its categories.
 * @return The report text, terminated by a newline.
 */
std::wstring FormatReport(const TraceReportHeader& header, const std::vector<std::wstring>& names,
                          bool with_categories);

/**
 * @brief Write text to a file as UTF-8.
 *
 * @param[in] path File to write.
 * @param[in] text Text to write.
 * @return An error text, empty when the file was written.
 */
std::wstring WriteUtf8File(const std::filesystem::path& path, const std::wstring& text);

} // namespace appbox::tracer

#endif // APPBOX_TRACER_TRACEREPORT_HPP
