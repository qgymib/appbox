#include "tracer/ArmPlan.hpp"
#include "tracer/CdbLocator.hpp"
#include "tracer/CdbSession.hpp"
#include "tracer/Console.hpp"
#include "tracer/Options.hpp"
#include "tracer/TargetProgram.hpp"
#include "tracer/TraceReport.hpp"
#include "tracer/TracedModules.hpp"
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Collect the wide command line of this process.
 *
 * The arguments are read from the process command line instead of a wmain entry
 * point, so the tool uses exactly the same parsing path as the unit tests and
 * the project does not need a second CRT entry point.
 *
 * @return The arguments, index 0 being the program name.
 */
std::vector<std::wstring> CommandLineArguments()
{
    int count = 0;
    LPWSTR* list = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    if (list == nullptr)
    {
        return {};
    }

    std::vector<std::wstring> arguments;
    arguments.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index)
    {
        arguments.emplace_back(list[index]);
    }

    ::LocalFree(list);
    return arguments;
}

/**
 * @brief Join the category names for the report header.
 *
 * @param[in] categories Categories to print.
 * @return Comma separated names.
 */
std::wstring JoinCategoryNames(const std::vector<appbox::tracer::Category>& categories)
{
    std::wstring text;
    for (const auto& name : appbox::tracer::CategoryNames(categories))
    {
        if (!text.empty())
        {
            text += L", ";
        }

        text += name;
    }

    return text;
}

/**
 * @brief Directory which holds the system modules the target runs against.
 *
 * The machine type of the target decides between the 64 bit and the 32 bit
 * system modules, because a 32 bit process runs against the WOW64 copies and
 * their export tables are the ones its calls end up in.
 *
 * @param[in] target Path of the program to trace.
 * @return The directory of the system modules.
 */
std::filesystem::path ModuleDirectoryForTarget(const std::filesystem::path& target)
{
    try
    {
        return appbox::tracer::SystemDirectoryForMachine(
            appbox::tracer::PeImage::FromFile(target).Machine());
    }
    catch (const std::runtime_error&)
    {
        /* Without a readable image the 64 bit system modules are the best guess. */
        return appbox::tracer::SystemDirectoryForMachine(0);
    }
}

/**
 * @brief Build the breakpoint plan of the traced modules.
 *
 * @param[in] options Parsed command line.
 * @param[in] target Path of the program to trace.
 * @param[out] directory Directory of the system modules.
 * @param[out] error Error text when the plan can not be built.
 * @return The plan, empty when it could not be built.
 */
std::vector<appbox::tracer::ArmGroup> BuildPlan(const appbox::tracer::Options& options,
                                                const std::filesystem::path& target,
                                                std::filesystem::path& directory,
                                                std::wstring& error)
{
    directory = ModuleDirectoryForTarget(target);
    const appbox::tracer::ModuleImages modules = appbox::tracer::LoadTracedModules(directory);
    if (modules.empty())
    {
        error = L"the system modules below '" + directory.wstring() + L"' could not be read";
        return {};
    }

    return appbox::tracer::BuildArmPlan(modules, options.categories, options.all_exports, directory);
}

/**
 * @brief Print the functions which a run would arm.
 *
 * @param[in] options Parsed command line.
 * @param[in] target Resolved path of the program.
 * @param[in] scope Text which describes the scope.
 * @return 0 when the listing was written, 1 when it could not be written.
 */
int ListScope(const appbox::tracer::Options& options, const std::filesystem::path& target,
              const std::wstring& scope)
{
    std::filesystem::path directory;
    std::wstring error;
    const auto plan = BuildPlan(options, target, directory, error);
    if (!error.empty())
    {
        appbox::tracer::WriteStderr(L"AppBoxTracer: " + error + L"\n");
        return 1;
    }

    const std::wstring text = appbox::tracer::FormatScope(plan, scope, options.with_categories);
    if (options.output_path.empty())
    {
        appbox::tracer::WriteStdout(text);
        return 0;
    }

    error = appbox::tracer::WriteUtf8File(options.output_path, text);
    if (!error.empty())
    {
        appbox::tracer::WriteStderr(L"AppBoxTracer: " + error + L"\n");
        return 1;
    }

    return 0;
}

/**
 * @brief Run the program below the debugger and write the report.
 *
 * @param[in] options Parsed command line.
 * @param[in] target Resolved path of the program.
 * @param[in] debugger Resolved path of the debugger.
 * @param[in] scope Text which describes the scope.
 * @return 0 when the trace completed, 1 when it did not.
 */
int Trace(const appbox::tracer::Options& options, const std::filesystem::path& target,
          const std::filesystem::path& debugger, const std::wstring& scope)
{
    std::filesystem::path directory;
    std::wstring error;
    const auto plan = BuildPlan(options, target, directory, error);
    if (!error.empty())
    {
        appbox::tracer::WriteStderr(L"AppBoxTracer: " + error + L"\n");
        return 1;
    }

    if (options.all_exports)
    {
        appbox::tracer::WriteStderr(
            L"AppBoxTracer: the exhaustive scope arms every executable export of the three "
            L"modules; arming them takes much longer than the category scope\n");
    }

    appbox::tracer::TraceRequest request;
    request.debugger = debugger;
    request.program = target;
    request.program_args = options.target_args;
    request.plan = plan;
    request.timeout_seconds = options.timeout_seconds;
    request.stall_timeout_seconds = options.stall_timeout_seconds;
    request.keep_raw_path = options.keep_raw_path;

    const appbox::tracer::TraceResult result = appbox::tracer::RunTraceSession(request);

    appbox::tracer::TraceReportHeader header;
    header.program = target.wstring();
    header.debugger = debugger.wstring();
    header.scope = scope;
    header.processes = result.processes;
    header.breakpoints = result.breakpoints;
    header.status = result.status == appbox::tracer::RunStatus::Completed
                        ? std::wstring(L"completed")
                        : L"aborted: " + result.message;

    const std::wstring report = appbox::tracer::FormatReport(header, result.names,
                                                             options.with_categories);
    if (options.output_path.empty())
    {
        appbox::tracer::WriteStdout(report);
    }
    else
    {
        error = appbox::tracer::WriteUtf8File(options.output_path, report);
        if (!error.empty())
        {
            appbox::tracer::WriteStderr(L"AppBoxTracer: " + error + L"\n");
            return 1;
        }
    }

    for (std::size_t index = 0; index < result.calls_per_process.size(); ++index)
    {
        appbox::tracer::WriteStderr(L"AppBoxTracer: process " + std::to_wstring(index + 1U) +
                                    L": " + std::to_wstring(result.calls_per_process[index]) +
                                    L" calls\n");
    }

    if (result.status == appbox::tracer::RunStatus::Completed)
    {
        appbox::tracer::WriteStderr(L"AppBoxTracer: " + std::to_wstring(result.names.size()) +
                                    L" functions of the scope were used\n");
        return 0;
    }

    appbox::tracer::WriteStderr(L"AppBoxTracer: the run did not complete: " + result.message +
                                L"\n");
    return 1;
}

} // namespace

/**
 * @brief Entry point of the tracer.
 *
 * The command line is parsed first, then the debugger is resolved and the
 * program is checked. A run arms one breakpoint per selected function in every
 * process and collects the marker lines the breakpoints print.
 *
 * @return 0 when the run completed, 1 when the trace could not be completed,
 *         2 when the command line is invalid.
 */
int main()
{
    const std::vector<std::wstring> arguments = CommandLineArguments();
    if (arguments.empty())
    {
        appbox::tracer::WriteStderr(L"AppBoxTracer: the command line could not be read\n");
        return 2;
    }

    appbox::tracer::Options options;
    const appbox::tracer::ParseResult result = appbox::tracer::ParseOptions(arguments, options);
    if (result.status == appbox::tracer::ParseStatus::Help)
    {
        appbox::tracer::WriteStdout(result.message);
        return 0;
    }

    if (result.status == appbox::tracer::ParseStatus::Error)
    {
        appbox::tracer::WriteStderr(result.message);
        return 2;
    }

    const std::filesystem::path target = appbox::tracer::ResolveTargetProgram(options.target_path);
    if (target.empty())
    {
        appbox::tracer::WriteStderr(L"AppBoxTracer: the program '" + options.target_path.wstring() +
                                    L"' was not found\n");
        return 2;
    }

    const std::wstring scope = options.all_exports ? std::wstring(L"all exports")
                                                   : JoinCategoryNames(options.categories);
    if (options.list_scope)
    {
        return ListScope(options, target, scope);
    }

    const std::filesystem::path debugger = appbox::tracer::ResolveCdb(options.cdb_path);
    if (debugger.empty())
    {
        appbox::tracer::WriteStderr(
            L"AppBoxTracer: cdb.exe was not found; pass its path with --cdb <path>\n");
        return 2;
    }

    return Trace(options, target, debugger, scope);
}
