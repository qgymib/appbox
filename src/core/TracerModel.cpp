#include "TracerModel.hpp"
#include "WString.hpp"
#include "tracer/ArmPlan.hpp"
#include "tracer/CdbLocator.hpp"
#include "tracer/TracedModules.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <set>
#include <utility>

namespace appbox
{
namespace
{

/**
 * @brief Sort rows by their name.
 *
 * `std::wstring` compares code unit by code unit, which is the ASCII order the workspace
 * promises for the function names.
 *
 * @param[in,out] entries Rows to sort.
 */
void SortByName(std::vector<TracerEntry>& entries)
{
    std::sort(entries.begin(), entries.end(),
              [](const TracerEntry& left, const TracerEntry& right) { return left.name < right.name; });
}

} // namespace

std::string TracerViewKey(TracerView view)
{
    return view == TracerView::AllExports ? "all-exports" : "scope";
}

const char* TracerViewDisplayName(TracerView view)
{
    return view == TracerView::AllExports ? "All exports" : "Isolation entry points";
}

bool BuildTracerView(TracerView view, const std::filesystem::path& module_directory, std::vector<TracerEntry>& entries,
                     std::string& error)
{
    entries.clear();
    error.clear();

    const appbox::tracer::ModuleImages modules = appbox::tracer::LoadTracedModules(module_directory);
    if (modules.empty())
    {
        error = "the system modules below '" + WideToUTF8(module_directory.wstring()) + "' could not be read";
        return false;
    }

    const std::vector<appbox::tracer::ArmGroup> plan = appbox::tracer::BuildArmPlan(
        modules, appbox::tracer::AllCategories(), view == TracerView::AllExports, module_directory);
    if (plan.empty())
    {
        error = "no function could be listed for the selected view";
        return false;
    }

    for (const auto& group : plan)
    {
        for (const auto& name : group.names)
        {
            entries.push_back(TracerEntry{ name, false });
        }
    }

    SortByName(entries);
    return true;
}

void MarkTracerEntries(std::vector<TracerEntry>& entries, const std::vector<std::wstring>& used)
{
    const std::set<std::wstring> used_set(used.begin(), used.end());
    for (auto& entry : entries)
    {
        entry.used = used_set.find(entry.name) != used_set.end();
    }
}

void OrderTracerEntries(std::vector<TracerEntry>& entries)
{
    SortByName(entries);
    std::stable_partition(entries.begin(), entries.end(), [](const TracerEntry& entry) { return entry.used; });
}

std::wstring TracerRunSummary(const appbox::tracer::TraceResult& result)
{
    if (result.status != appbox::tracer::RunStatus::Completed)
    {
        return L"the run did not complete: " + result.message;
    }

    return L"processes: " + std::to_wstring(result.processes) + L", breakpoints: " +
           std::to_wstring(result.breakpoints) + L", functions used: " + std::to_wstring(result.names.size());
}

TracerRunOutcome RunTracerSession(const std::filesystem::path& target, const std::vector<std::wstring>& program_args,
                                  TracerView view, const std::filesystem::path& module_directory,
                                  const std::function<void(const std::wstring&)>& progress)
{
    TracerRunOutcome outcome;

    const std::filesystem::path debugger = appbox::tracer::ResolveCdb(std::filesystem::path());
    if (debugger.empty())
    {
        outcome.error = L"cdb.exe was not found; install the Debugging Tools for Windows";
        return outcome;
    }

    const appbox::tracer::ModuleImages modules = appbox::tracer::LoadTracedModules(module_directory);
    if (modules.empty())
    {
        outcome.error = L"the system modules below '" + module_directory.wstring() + L"' could not be read";
        return outcome;
    }

    appbox::tracer::TraceRequest request;
    request.debugger = debugger;
    request.program = target;
    request.program_args = program_args;
    request.plan = appbox::tracer::BuildArmPlan(modules, appbox::tracer::AllCategories(),
                                                view == TracerView::AllExports, module_directory);
    request.timeout_seconds = kTracerRunTimeoutSeconds;
    request.stall_timeout_seconds = kTracerStallTimeoutSeconds;
    request.progress = progress;

    outcome.result = appbox::tracer::RunTraceSession(request);
    return outcome;
}

bool SaveTracerExport(const std::wstring& path, const std::wstring& program, TracerView view,
                      const appbox::tracer::TraceResult& result, const std::vector<TracerEntry>& entries,
                      std::string& error)
{
    error.clear();
    if (path.empty())
    {
        error = "no export path was given";
        return false;
    }

    try
    {
        nlohmann::ordered_json document;
        document["program"] = WideToUTF8(program);
        document["view"] = TracerViewKey(view);
        document["result"] = result.status == appbox::tracer::RunStatus::Completed
                                 ? std::string("completed")
                                 : "aborted: " + WideToUTF8(result.message);
        document["processes"] = result.processes;
        document["breakpoints"] = result.breakpoints;
        document["functions"] = nlohmann::ordered_json::array();
        for (const auto& entry : entries)
        {
            if (entry.used)
            {
                document["functions"].push_back(WideToUTF8(entry.name));
            }
        }

        const std::string text = document.dump(2);
        std::ofstream     out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            error = "cannot create the export file: " + WideToUTF8(path);
            return false;
        }

        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out.good())
        {
            error = "cannot write the export file: " + WideToUTF8(path);
            return false;
        }
    }
    catch (const std::exception& exception)
    {
        error = std::string("cannot write the export file: ") + exception.what();
        return false;
    }

    return true;
}

} // namespace appbox
