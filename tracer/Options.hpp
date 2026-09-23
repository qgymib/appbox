#ifndef APPBOX_TRACER_OPTIONS_HPP
#define APPBOX_TRACER_OPTIONS_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace appbox::tracer
{

/** Default hard limit of one trace run, in seconds. */
inline constexpr unsigned kDefaultTimeoutSeconds = 600;

/** Default time without debugger output after which a run is aborted. */
inline constexpr unsigned kDefaultStallTimeoutSeconds = 30;

/**
 * @brief Categories of traced functions.
 *
 * The categories mirror the isolation domains of appbox (filesystem, registry
 * and network isolation), so the report answers the question which of the
 * functions an isolation layer has to intercept are used by a program.
 */
enum class Category
{
    File,      ///< Filesystem related exports.
    Registry,  ///< Registry related exports.
    Network,   ///< Network related exports (named pipes, mailslots, device control).
};

/** Parsed command line of the tracer. */
struct Options
{
    std::filesystem::path cdb_path;       ///< `--cdb`; empty means search for the debugger.
    std::filesystem::path output_path;    ///< `--output`; empty means the standard output.
    std::filesystem::path keep_raw_path;  ///< `--keep-raw`; empty means the raw output is dropped.
    std::vector<Category> categories;     ///< `--categories`; empty means every export (see all_exports).
    bool all_exports = false;             ///< `--all-exports`: trace every executable export.
    bool list_scope = false;              ///< `--list-scope`: print the armed functions and exit.
    bool with_categories = false;         ///< `--with-categories`: annotate the report with categories.
    unsigned timeout_seconds = kDefaultTimeoutSeconds;             ///< `--timeout`.
    unsigned stall_timeout_seconds = kDefaultStallTimeoutSeconds;  ///< `--stall-timeout`.
    std::filesystem::path target_path;    ///< Program to run; required.
    std::vector<std::wstring> target_args; ///< Arguments passed to the program.
};

/** Status of a parsed command line. */
enum class ParseStatus
{
    Ok,     ///< The options are complete and consistent.
    Help,   ///< `--help` was given; print the usage text.
    Error,  ///< The command line is invalid; print the error text.
};

/** Result of ParseOptions. */
struct ParseResult
{
    ParseStatus status = ParseStatus::Ok;  ///< Outcome of the parse.
    std::wstring message;                  ///< Usage text for Help, error text for Error.
};

/**
 * @brief Parse the wide command line of the tracer.
 *
 * The first element of `arguments` is the program name, exactly as in `argv`;
 * it is used as the application name and is never treated as an option or as
 * the target. Option parsing stops at the first positional argument, so the
 * arguments of the target are passed through unchanged.
 *
 * @param[in] arguments Command line, index 0 being the program name.
 * @param[out] options Filled with the parsed options when the status is Ok.
 * @return The parse status together with the text to print for Help and Error.
 */
ParseResult ParseOptions(const std::vector<std::wstring>& arguments, Options& options);

/** @return Every category, in the order they are documented and printed. */
std::vector<Category> AllCategories();

/**
 * @brief Name of a category as used on the command line and in the report.
 *
 * @param[in] category Category to name.
 * @return The name, or an empty string for a value outside the enumeration.
 */
std::wstring CategoryName(Category category);

/**
 * @brief Names of the given categories, in the order of the input.
 *
 * @param[in] categories Categories to name.
 * @return The names, one per category.
 */
std::vector<std::wstring> CategoryNames(const std::vector<Category>& categories);

/**
 * @brief Parse a comma separated category list.
 *
 * @param[in] text Text such as `file,registry`.
 * @param[out] categories Parsed categories; unchanged on failure.
 * @param[out] error Error text when the text is not a category list.
 * @return Whether the text could be parsed.
 */
bool ParseCategories(const std::wstring& text, std::vector<Category>& categories, std::wstring& error);

/** @return The usage text of the tool. */
std::wstring UsageText();

} // namespace appbox::tracer

#endif // APPBOX_TRACER_OPTIONS_HPP
