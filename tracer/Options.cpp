#include "tracer/Options.hpp"
#include "WString.hpp"
#include <CLI/CLI.hpp>
#include <algorithm>
#include <cwctype>
#include <string>

namespace appbox::tracer
{
namespace
{

/** Command line name of one traced category. */
struct CategoryNameEntry
{
    Category category;   ///< Category value.
    const wchar_t* name; ///< Name used on the command line and in the report.
};

/** Categories in the order they are documented and printed. */
constexpr CategoryNameEntry kCategoryNames[] = {
    {Category::File, L"file"},
    {Category::Registry, L"registry"},
    {Category::Network, L"network"},
};

/**
 * @brief Name of a category.
 *
 * @param[in] category Category to look up.
 * @return The name, or an empty string for a value outside the enumeration.
 */
std::wstring LookupCategoryName(Category category)
{
    for (const auto& entry : kCategoryNames)
    {
        if (entry.category == category)
        {
            return entry.name;
        }
    }

    return {};
}

/**
 * @brief Lowercase a wide string.
 *
 * @param[in] text Text to convert.
 * @return The lowercased text.
 */
std::wstring ToLower(const std::wstring& text)
{
    std::wstring result = text;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return result;
}

/**
 * @brief Remove spaces and tabs around a wide string.
 *
 * @param[in] text Text to trim.
 * @return The trimmed text.
 */
std::wstring Trim(const std::wstring& text)
{
    const auto first = text.find_first_not_of(L" \t");
    if (first == std::wstring::npos)
    {
        return {};
    }

    const auto last = text.find_last_not_of(L" \t");
    return text.substr(first, last - first + 1);
}

} // namespace

std::vector<Category> AllCategories()
{
    std::vector<Category> categories;
    for (const auto& entry : kCategoryNames)
    {
        categories.push_back(entry.category);
    }

    return categories;
}

std::wstring CategoryName(Category category)
{
    return LookupCategoryName(category);
}

std::vector<std::wstring> CategoryNames(const std::vector<Category>& categories)
{
    std::vector<std::wstring> names;
    names.reserve(categories.size());
    for (const auto& category : categories)
    {
        names.push_back(LookupCategoryName(category));
    }

    return names;
}

bool ParseCategories(const std::wstring& text, std::vector<Category>& categories, std::wstring& error)
{
    std::vector<Category> parsed;
    for (const auto& part : appbox::Split(text, L","))
    {
        const std::wstring trimmed = Trim(part);
        if (trimmed.empty())
        {
            continue;
        }

        const std::wstring name = ToLower(trimmed);
        bool found = false;
        for (const auto& entry : kCategoryNames)
        {
            if (name == entry.name)
            {
                found = true;
                if (std::find(parsed.begin(), parsed.end(), entry.category) == parsed.end())
                {
                    parsed.push_back(entry.category);
                }

                break;
            }
        }

        if (!found)
        {
            error = L"unknown category '" + trimmed + L"': expected file, registry or network";
            return false;
        }
    }

    if (parsed.empty())
    {
        error = L"no category given: expected file, registry or network";
        return false;
    }

    categories = parsed;
    return true;
}

std::wstring UsageText()
{
    return L"AppBoxTracer - report the functions of ntdll, kernel32 and kernelbase a program uses.\n"
           L"\n"
           L"Usage:\n"
           L"  AppBoxTracer [options] <program> [program arguments...]\n"
           L"\n"
           L"Options:\n"
           L"  --cdb <path>            Path of cdb.exe; searched on the PATH and below the\n"
           L"                          Windows Kits directory when it is omitted.\n"
           L"  --output <path>         Write the report as UTF-8 to this file instead of the\n"
           L"                          standard output.\n"
           L"  --categories <list>     Comma separated categories to trace: file, registry,\n"
           L"                          network. Default: all three.\n"
           L"  --all-exports           Trace every executable export of the three modules\n"
           L"                          instead of the categories. Much slower.\n"
           L"  --list-scope            Print the functions which would be armed and exit.\n"
           L"  --with-categories       Annotate every reported function with its categories.\n"
           L"  --timeout <seconds>     Hard limit of the whole run. Default: 600.\n"
           L"  --stall-timeout <s>     Seconds without debugger output before the run is\n"
           L"                          aborted and the collected result is written.\n"
           L"                          Default: 30.\n"
           L"  --keep-raw <path>       Write the raw debugger output as UTF-8 to this file.\n"
           L"  -h, --help              Print this text.\n"
           L"\n"
           L"Options must precede the program: everything from the program on is passed to\n"
           L"it unchanged. Child processes of the program are traced as well.\n"
           L"\n"
           L"Exit codes: 0 success, 1 the trace could not be completed, 2 invalid command\n"
           L"line.\n";
}

ParseResult ParseOptions(const std::vector<std::wstring>& arguments, Options& options)
{
    std::string cdb;
    std::string output;
    std::string keep_raw;
    std::string categories;
    bool all_exports = false;
    bool list_scope = false;
    bool with_categories = false;
    unsigned timeout_seconds = kDefaultTimeoutSeconds;
    unsigned stall_timeout_seconds = kDefaultStallTimeoutSeconds;

    CLI::App app{"Report the file, registry and network functions of ntdll, kernel32 and "
                 "kernelbase which a program and its child processes use."};
    app.prefix_command();
    app.add_option("--cdb", cdb, "Path of cdb.exe; searched when omitted");
    app.add_option("--output", output, "UTF-8 report file; the standard output when omitted");
    app.add_option("--categories", categories, "Comma separated categories: file, registry, network");
    app.add_flag("--all-exports", all_exports, "Trace every executable export of the three modules");
    app.add_flag("--list-scope", list_scope, "Print the functions which would be armed and exit");
    app.add_flag("--with-categories", with_categories, "Annotate every function with its categories");
    app.add_option("--timeout", timeout_seconds, "Hard limit of the whole run in seconds");
    app.add_option("--stall-timeout", stall_timeout_seconds, "Seconds without debugger output before abort");
    app.add_option("--keep-raw", keep_raw, "Write the raw debugger output to this UTF-8 file");

    /*
     * CLI11 consumes its vector argument from the back, so the vector has to be
     * reversed and must not contain the program name; that is exactly what its
     * own argc/argv overload does before it parses.
     */
    std::vector<std::string> utf8_arguments;
    utf8_arguments.reserve(arguments.size());
    for (std::size_t index = arguments.size(); index > 1; --index)
    {
        utf8_arguments.push_back(appbox::WideToUTF8(arguments[index - 1]));
    }

    try
    {
        app.parse(utf8_arguments);
    }
    catch (const CLI::CallForHelp&)
    {
        return {ParseStatus::Help, UsageText()};
    }
    catch (const CLI::ParseError& error)
    {
        return {ParseStatus::Error,
                L"invalid command line: " + appbox::UTF8ToWide(error.what()) + L"\n\n" + UsageText()};
    }

    if (all_exports && !categories.empty())
    {
        return {ParseStatus::Error,
                std::wstring(L"--all-exports and --categories can not be combined\n\n") + UsageText()};
    }

    if (timeout_seconds == 0 || stall_timeout_seconds == 0)
    {
        return {ParseStatus::Error,
                std::wstring(L"--timeout and --stall-timeout must be at least 1 second\n\n") + UsageText()};
    }

    const auto remaining = app.remaining();
    if (remaining.empty())
    {
        return {ParseStatus::Error, std::wstring(L"no program given\n\n") + UsageText()};
    }

    options.cdb_path = cdb.empty() ? std::filesystem::path() : std::filesystem::path(CLI::widen(cdb));
    options.output_path = output.empty() ? std::filesystem::path() : std::filesystem::path(CLI::widen(output));
    options.keep_raw_path =
        keep_raw.empty() ? std::filesystem::path() : std::filesystem::path(CLI::widen(keep_raw));
    options.all_exports = all_exports;
    options.list_scope = list_scope;
    options.with_categories = with_categories;
    options.timeout_seconds = timeout_seconds;
    options.stall_timeout_seconds = stall_timeout_seconds;
    options.target_path = CLI::widen(remaining.front());
    for (std::size_t index = 1; index < remaining.size(); ++index)
    {
        options.target_args.push_back(CLI::widen(remaining[index]));
    }

    if (all_exports)
    {
        /* Every export is the scope, so no category is selected. */
        options.categories.clear();
        return {ParseStatus::Ok, {}};
    }

    if (categories.empty())
    {
        options.categories = AllCategories();
        return {ParseStatus::Ok, {}};
    }

    std::wstring error;
    if (!ParseCategories(CLI::widen(categories), options.categories, error))
    {
        return {ParseStatus::Error, error + L"\n\n" + UsageText()};
    }

    return {ParseStatus::Ok, {}};
}

} // namespace appbox::tracer
