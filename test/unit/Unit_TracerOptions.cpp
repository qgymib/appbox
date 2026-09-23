#include <gtest/gtest.h>
#include "tracer/Options.hpp"
#include <string>
#include <vector>

namespace
{

/**
 * @brief Build a command line with the program name in front.
 *
 * @param[in] values Arguments which follow the program name.
 * @return The command line, index 0 being the program name.
 */
std::vector<std::wstring> CommandLine(std::initializer_list<std::wstring> values)
{
    std::vector<std::wstring> arguments{L"AppBoxTracer.exe"};
    arguments.insert(arguments.end(), values);
    return arguments;
}

/**
 * @brief Report whether a text contains a fragment.
 *
 * @param[in] text Text to search in.
 * @param[in] fragment Fragment to search for.
 * @return Whether the fragment occurs in the text.
 */
bool Contains(const std::wstring& text, const std::wstring& fragment)
{
    return text.find(fragment) != std::wstring::npos;
}

} // namespace

/**
 * @brief The usage text is printed when the help option is used, and no program
 *        is required in that case.
 */
TEST(TracerOptions, HelpIsReportedWithoutAProgram)
{
    appbox::tracer::Options options;
    const auto result = appbox::tracer::ParseOptions(CommandLine({L"--help"}), options);

    EXPECT_EQ(result.status, appbox::tracer::ParseStatus::Help);
    EXPECT_TRUE(Contains(result.message, L"Usage:"));
}

/**
 * @brief The program is the only mandatory argument.
 */
TEST(TracerOptions, MissingProgramIsAnError)
{
    appbox::tracer::Options options;
    const auto result = appbox::tracer::ParseOptions(CommandLine({}), options);

    EXPECT_EQ(result.status, appbox::tracer::ParseStatus::Error);
    EXPECT_TRUE(Contains(result.message, L"no program given"));
}

/**
 * @brief The program name of the tracer itself is never mistaken for the
 *        target: the target is the first argument after the options.
 */
TEST(TracerOptions, ProgramAndArgumentsAreCollected)
{
    appbox::tracer::Options options;
    const auto result =
        appbox::tracer::ParseOptions(CommandLine({L"cmd.exe", L"/c", L"echo", L"hi"}), options);

    EXPECT_EQ(result.status, appbox::tracer::ParseStatus::Ok);
    EXPECT_EQ(options.target_path.wstring(), L"cmd.exe");
    ASSERT_EQ(options.target_args.size(), 3U);
    EXPECT_EQ(options.target_args[0], L"/c");
    EXPECT_EQ(options.target_args[1], L"echo");
    EXPECT_EQ(options.target_args[2], L"hi");
}

/**
 * @brief Everything from the program on is passed through unchanged, so an
 *        option which follows the program belongs to the program.
 */
TEST(TracerOptions, OptionsAfterTheProgramBelongToTheProgram)
{
    appbox::tracer::Options options;
    const auto result = appbox::tracer::ParseOptions(CommandLine({L"cmd.exe", L"--list-scope"}), options);

    EXPECT_EQ(result.status, appbox::tracer::ParseStatus::Ok);
    EXPECT_FALSE(options.list_scope);
    ASSERT_EQ(options.target_args.size(), 1U);
    EXPECT_EQ(options.target_args[0], L"--list-scope");
}

/**
 * @brief Paths, flags and timeouts are taken over as they were given.
 */
TEST(TracerOptions, OptionsBeforeTheProgramAreParsed)
{
    appbox::tracer::Options options;
    const auto result = appbox::tracer::ParseOptions(
        CommandLine({L"--cdb", L"C:\\dbg\\cdb.exe", L"--output", L"report.txt", L"--keep-raw",
                     L"raw.txt", L"--with-categories", L"--list-scope", L"--timeout", L"42",
                     L"--stall-timeout", L"7", L"cmd.exe", L"/c"}),
        options);

    EXPECT_EQ(result.status, appbox::tracer::ParseStatus::Ok);
    EXPECT_EQ(options.cdb_path.wstring(), L"C:\\dbg\\cdb.exe");
    EXPECT_EQ(options.output_path.wstring(), L"report.txt");
    EXPECT_EQ(options.keep_raw_path.wstring(), L"raw.txt");
    EXPECT_TRUE(options.with_categories);
    EXPECT_TRUE(options.list_scope);
    EXPECT_EQ(options.timeout_seconds, 42U);
    EXPECT_EQ(options.stall_timeout_seconds, 7U);
    EXPECT_EQ(options.target_path.wstring(), L"cmd.exe");
    ASSERT_EQ(options.target_args.size(), 1U);
    EXPECT_EQ(options.target_args[0], L"/c");
}

/**
 * @brief Without a category list the scope is the union of the three isolation
 *        domains.
 */
TEST(TracerOptions, DefaultScopeIsEveryCategory)
{
    appbox::tracer::Options options;
    const auto result = appbox::tracer::ParseOptions(CommandLine({L"cmd.exe"}), options);

    EXPECT_EQ(result.status, appbox::tracer::ParseStatus::Ok);
    EXPECT_FALSE(options.all_exports);
    EXPECT_EQ(options.categories, appbox::tracer::AllCategories());
}

/**
 * @brief The category list accepts spaces and duplicates, and keeps the order
 *        of the first occurrence.
 */
TEST(TracerOptions, CategoriesAreParsedAndDeduplicated)
{
    appbox::tracer::Options options;
    const auto result = appbox::tracer::ParseOptions(
        CommandLine({L"--categories", L"network, file ,network", L"cmd.exe"}), options);

    EXPECT_EQ(result.status, appbox::tracer::ParseStatus::Ok);
    ASSERT_EQ(options.categories.size(), 2U);
    EXPECT_EQ(options.categories[0], appbox::tracer::Category::Network);
    EXPECT_EQ(options.categories[1], appbox::tracer::Category::File);
}

/**
 * @brief A typo in the category list is reported instead of being ignored.
 */
TEST(TracerOptions, UnknownCategoryIsAnError)
{
    appbox::tracer::Options options;
    const auto result =
        appbox::tracer::ParseOptions(CommandLine({L"--categories", L"files", L"cmd.exe"}), options);

    EXPECT_EQ(result.status, appbox::tracer::ParseStatus::Error);
    EXPECT_TRUE(Contains(result.message, L"unknown category 'files'"));
}

/**
 * @brief An empty category list is an error as well: the scope must be clear.
 */
TEST(TracerOptions, EmptyCategoryListIsAnError)
{
    appbox::tracer::Options options;
    const auto result =
        appbox::tracer::ParseOptions(CommandLine({L"--categories", L" , ", L"cmd.exe"}), options);

    EXPECT_EQ(result.status, appbox::tracer::ParseStatus::Error);
    EXPECT_TRUE(Contains(result.message, L"no category given"));
}

/**
 * @brief The exhaustive scope replaces the category list, so both can not be
 *        requested at the same time.
 */
TEST(TracerOptions, AllExportsAndCategoriesConflict)
{
    appbox::tracer::Options options;
    const auto result = appbox::tracer::ParseOptions(
        CommandLine({L"--all-exports", L"--categories", L"file", L"cmd.exe"}), options);

    EXPECT_EQ(result.status, appbox::tracer::ParseStatus::Error);
    EXPECT_TRUE(Contains(result.message, L"can not be combined"));
}

/**
 * @brief The exhaustive scope leaves the category list empty, which is what the
 *        report prints as its scope.
 */
TEST(TracerOptions, AllExportsClearsTheCategories)
{
    appbox::tracer::Options options;
    const auto result = appbox::tracer::ParseOptions(CommandLine({L"--all-exports", L"cmd.exe"}), options);

    EXPECT_EQ(result.status, appbox::tracer::ParseStatus::Ok);
    EXPECT_TRUE(options.all_exports);
    EXPECT_TRUE(options.categories.empty());
}

/**
 * @brief A timeout of zero would abort every run immediately, so it is
 *        rejected.
 */
TEST(TracerOptions, ZeroTimeoutIsAnError)
{
    appbox::tracer::Options options;
    EXPECT_EQ(appbox::tracer::ParseOptions(CommandLine({L"--timeout", L"0", L"cmd.exe"}), options).status,
              appbox::tracer::ParseStatus::Error);
    EXPECT_EQ(appbox::tracer::ParseOptions(
                  CommandLine({L"--stall-timeout", L"0", L"cmd.exe"}), options).status,
              appbox::tracer::ParseStatus::Error);
}

/**
 * @brief A timeout which is not a number is reported as an invalid command
 *        line.
 */
TEST(TracerOptions, InvalidTimeoutIsAnError)
{
    appbox::tracer::Options options;
    const auto result =
        appbox::tracer::ParseOptions(CommandLine({L"--timeout", L"soon", L"cmd.exe"}), options);

    EXPECT_EQ(result.status, appbox::tracer::ParseStatus::Error);
    EXPECT_TRUE(Contains(result.message, L"invalid command line"));
}

/**
 * @brief The category names used on the command line are the ones the report
 *        prints.
 */
TEST(TracerOptions, CategoryNamesMatchTheCommandLine)
{
    EXPECT_EQ(appbox::tracer::CategoryName(appbox::tracer::Category::File), L"file");
    EXPECT_EQ(appbox::tracer::CategoryName(appbox::tracer::Category::Registry), L"registry");
    EXPECT_EQ(appbox::tracer::CategoryName(appbox::tracer::Category::Network), L"network");

    const auto names = appbox::tracer::CategoryNames(appbox::tracer::AllCategories());
    ASSERT_EQ(names.size(), 3U);
    EXPECT_EQ(names[0], L"file");
    EXPECT_EQ(names[1], L"registry");
    EXPECT_EQ(names[2], L"network");
}

/**
 * @brief The category parser is usable on its own, which keeps the report free
 *        of a second spelling of the names.
 */
TEST(TracerOptions, CategoriesCanBeParsedDirectly)
{
    std::vector<appbox::tracer::Category> categories;
    std::wstring error;

    EXPECT_TRUE(appbox::tracer::ParseCategories(L"registry", categories, error));
    ASSERT_EQ(categories.size(), 1U);
    EXPECT_EQ(categories[0], appbox::tracer::Category::Registry);

    EXPECT_FALSE(appbox::tracer::ParseCategories(L"", categories, error));
    EXPECT_FALSE(error.empty());
}
