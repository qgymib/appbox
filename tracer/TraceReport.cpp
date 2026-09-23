#include "tracer/TraceReport.hpp"
#include "tracer/ScopePatterns.hpp"
#include "WString.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <set>

namespace appbox::tracer
{
namespace
{

/**
 * @brief Text which annotates a name with its categories.
 *
 * @param[in] name `module!function` name of the function.
 * @return `  [file, network]` and alike, or an empty string when the name has
 *         no category (which happens for the exhaustive scope).
 */
std::wstring CategoryAnnotation(const std::wstring& name)
{
    /* The classifier works on the exported name, not on the module prefix. */
    const auto separator = name.find(L'!');
    const std::wstring plain_name =
        separator == std::wstring::npos ? name : name.substr(separator + 1U);

    const std::vector<Category> categories = ClassifyExport(plain_name);
    if (categories.empty())
    {
        return {};
    }

    std::wstring text = L"  [";
    bool first = true;
    for (const auto& category : categories)
    {
        if (!first)
        {
            text += L", ";
        }

        text += CategoryName(category);
        first = false;
    }

    text += L"]";
    return text;
}

} // namespace

std::wstring FormatScope(const std::vector<ArmGroup>& plan, const std::wstring& scope,
                         bool with_categories)
{
    /* The names are grouped by the module which holds the implementation, which
     * is the module a breakpoint is placed in. */
    std::map<std::wstring, std::set<std::wstring>> by_module;
    for (const auto& group : plan)
    {
        for (const auto& name : group.names)
        {
            by_module[group.module].insert(name);
        }
    }

    std::wstring text = L"Scope: " + scope + L"\n";
    text += L"Breakpoints: " + std::to_wstring(plan.size()) + L"\n";

    for (const auto& entry : by_module)
    {
        const std::size_t breakpoints = static_cast<std::size_t>(
            std::count_if(plan.begin(), plan.end(), [&entry](const ArmGroup& group) {
                return group.module == entry.first;
            }));

        text += L"\n" + entry.first + L".dll: " + std::to_wstring(breakpoints) +
                L" breakpoints, " + std::to_wstring(entry.second.size()) + L" names\n";
        for (const auto& name : entry.second)
        {
            text += L"  " + name;
            if (with_categories)
            {
                text += CategoryAnnotation(name);
            }

            text += L"\n";
        }
    }

    return text;
}

std::wstring FormatReport(const TraceReportHeader& header, const std::vector<std::wstring>& names,
                          bool with_categories)
{
    std::wstring text = L"AppBoxTracer report\n";
    text += L"  program     : " + header.program + L"\n";
    text += L"  debugger    : " + header.debugger + L"\n";
    text += L"  scope       : " + header.scope + L"\n";
    text += L"  processes   : " + std::to_wstring(header.processes) + L"\n";
    text += L"  breakpoints : " + std::to_wstring(header.breakpoints) + L" per process\n";
    text += L"  result      : " + header.status + L"\n\n";

    /* The name carries the module which exports the function; that is the
     * section the report is grouped by. */
    std::map<std::wstring, std::set<std::wstring>> by_module;
    for (const auto& name : names)
    {
        const auto separator = name.find(L'!');
        const std::wstring module =
            separator == std::wstring::npos ? std::wstring() : name.substr(0, separator);
        by_module[module].insert(name);
    }

    if (by_module.empty())
    {
        text += L"no function of the scope was used\n";
        return text;
    }

    for (const auto& module : by_module)
    {
        text += module.first + L".dll (" + std::to_wstring(module.second.size()) + L" functions)\n";
        for (const auto& name : module.second)
        {
            text += L"  " + name;
            if (with_categories)
            {
                text += CategoryAnnotation(name);
            }

            text += L"\n";
        }

        text += L"\n";
    }

    return text;
}

std::wstring WriteUtf8File(const std::filesystem::path& path, const std::wstring& text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        return L"the file '" + path.wstring() + L"' could not be opened for writing";
    }

    try
    {
        const std::string utf8 = appbox::WideToUTF8(text);
        stream.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    }
    catch (const std::exception&)
    {
        return L"the text could not be encoded for '" + path.wstring() + L"'";
    }

    if (!stream)
    {
        return L"the file '" + path.wstring() + L"' could not be written completely";
    }

    return {};
}

} // namespace appbox::tracer
