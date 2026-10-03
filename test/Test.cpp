#include "sandbox/utils/WinAPI.h" /* Must be included before any other headers. */
#include <gtest/gtest.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <spdlog/spdlog.h>
#include "utils/CommandLine.hpp"
#include "Test.hpp"
#include "WString.hpp"

appbox::test::TestConfig appbox::test::config;

namespace
{

/** Separator of the patterns of a GoogleTest filter. */
constexpr char kPatternSeparator = ':';

/** Separator of the positive and the negative part of a GoogleTest filter. */
constexpr char kNegationSeparator = '-';

/** The GoogleTest filter which matches every test. */
constexpr const char* kUniversalFilter = "*";

/** Name of the directory which holds the end-to-end cases. */
constexpr const char* kEndToEndDirectory = "e2e";

/** Name of the directory which holds the unit tests. */
constexpr const char* kUnitDirectory = "unit";

/** The positive and the negative part of a GoogleTest filter. */
struct FilterParts
{
    std::string positive;
    std::string negative;
};

/**
 * @brief Split a GoogleTest filter into its two parts.
 *
 * GoogleTest splits the filter at every `-` and joins the parts behind the
 * first one back together, so the part before the first separator is the
 * positive filter and the remainder is the negative one.
 *
 * @param[in] filter The filter of the run.
 * @return The parts of the filter.
 */
FilterParts SplitFilter(const std::string& filter)
{
    FilterParts parts;

    const auto separator = filter.find(kNegationSeparator);
    if (separator == std::string::npos)
    {
        parts.positive = filter;
        return parts;
    }

    parts.positive = filter.substr(0, separator);
    parts.negative = filter.substr(separator + 1);
    return parts;
}

/**
 * @brief Append a pattern to a part of a filter.
 * @param[in,out] part The part to extend.
 * @param[in] pattern The pattern to append, ignored when it is empty.
 */
void AppendPattern(std::string& part, const std::string& pattern)
{
    if (pattern.empty())
    {
        return;
    }

    if (!part.empty())
    {
        part += kPatternSeparator;
    }
    part += pattern;
}

/**
 * @brief Check whether a text starts with a prefix.
 * @param[in] text Text to check.
 * @param[in] prefix Prefix to look for.
 * @return true when the text starts with the prefix.
 */
bool StartsWith(const std::string& text, const char* prefix)
{
    return text.rfind(prefix, 0) == 0;
}

/**
 * @brief Build the pattern which matches the suites of the other mode.
 *
 * The pattern is appended to the negative part of the filter of a run, so the
 * suites of the other mode stay out of the run whatever the positive filter of
 * the command line names.
 *
 * @param[in] mode The mode of the run.
 * @return The pattern of the suites which the mode excludes.
 */
std::string ExcludedSuitePattern(appbox::test::TestMode mode)
{
    const char* prefix =
        mode == appbox::test::TestMode::Unit ? appbox::test::kEndToEndSuitePrefix : appbox::test::kUnitSuitePrefix;
    return std::string(prefix) + kUniversalFilter;
}

/**
 * @brief Check whether a source file lives in a directory.
 *
 * The comparison ignores the case and accepts both path separators, and it
 * matches a whole component of the path, so `test/e2e/Fs.cpp` is inside `e2e`
 * while `test/ene2e/Fs.cpp` is not.
 *
 * @param[in] file The path of the source file, may be null.
 * @param[in] directory Name of the directory.
 * @return true when the file lives in that directory.
 */
bool IsInDirectory(const char* file, const char* directory)
{
    if (file == nullptr)
    {
        return false;
    }

    std::string path(file);
    std::transform(path.begin(), path.end(), path.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    std::replace(path.begin(), path.end(), '\\', '/');

    const std::string prefix = std::string(directory) + "/";
    if (path.rfind(prefix, 0) == 0)
    {
        return true;
    }

    return path.find(std::string("/") + prefix) != std::string::npos;
}

/**
 * @brief Check that every registered suite carries the prefix of its directory.
 *
 * The unit tests live below `test/unit` and the end-to-end cases below
 * `test/e2e`, so the directory of the source file of a suite decides the mode
 * it is run in and the prefix of its name has to agree with that directory. A
 * suite which disagrees would be run by the other mode or by neither of them;
 * both are refused here, before the first test case runs.
 *
 * @param[out] error Description of the first suite which breaks the rule.
 * @return true when every suite belongs to a side.
 */
bool CheckSuiteOwnership(std::string& error)
{
    const auto* unit_test = ::testing::UnitTest::GetInstance();

    for (int index = 0; index < unit_test->total_test_suite_count(); ++index)
    {
        const auto* suite = unit_test->GetTestSuite(index);
        if (suite == nullptr || suite->total_test_count() == 0)
        {
            continue;
        }

        const auto*       info = suite->GetTestInfo(0);
        const auto*       file = info != nullptr ? info->file() : nullptr;
        const char*       path = file != nullptr ? file : "an unknown file";
        const std::string name = suite->name();

        if (IsInDirectory(file, kEndToEndDirectory))
        {
            if (!StartsWith(name, appbox::test::kEndToEndSuitePrefix))
            {
                error = fmt::format("the suite `{}` of `{}` does not start with `{}`", name, path,
                                    appbox::test::kEndToEndSuitePrefix);
                return false;
            }
        }
        else if (IsInDirectory(file, kUnitDirectory))
        {
            if (!StartsWith(name, appbox::test::kUnitSuitePrefix))
            {
                error = fmt::format("the suite `{}` of `{}` does not start with `{}`", name, path,
                                    appbox::test::kUnitSuitePrefix);
                return false;
            }
        }
        else
        {
            error =
                fmt::format("the suite `{}` is defined in `{}`, which is neither test/e2e nor test/unit", name, path);
            return false;
        }
    }

    return true;
}

/** @brief Take the level of the log files of a case out of a command line value. */
void SetCaseLogLevelFromString(const std::wstring& level)
{
    appbox::test::config.log_level = level;
}

/**
 * @brief Take the mode out of a command line value.
 * @param[in] text The value of the option.
 * @throws CLI::ValidationError when the value is not a known mode.
 */
void SetModeFromString(const std::wstring& text)
{
    if (!appbox::test::ParseTestMode(text, appbox::test::config.mode))
    {
        throw CLI::ValidationError("--mode", "unknown test mode: " + appbox::WideToUTF8(text));
    }
}

void SetConfigFromEnv()
{
    std::wstring val;
    if (appbox::test::ReadEnvironmentVariable(L"APPBOX_TEST_LAUNCHER", val))
    {
        appbox::test::config.launcher_path = val;
    }
    if (appbox::test::ReadEnvironmentVariable(L"APPBOX_TEST_SANDBOX32", val))
    {
        appbox::test::config.sandbox32_path = val;
    }
    if (appbox::test::ReadEnvironmentVariable(L"APPBOX_TEST_SANDBOX64", val))
    {
        appbox::test::config.sandbox64_path = val;
    }
    if (appbox::test::ReadEnvironmentVariable(L"APPBOX_TEST_PACKER", val))
    {
        appbox::test::config.packer_path = val;
    }
    if (appbox::test::ReadEnvironmentVariable(L"APPBOX_TEST_LOG_LEVEL", val))
    {
        SetCaseLogLevelFromString(val);
    }
    if (appbox::test::ReadEnvironmentVariable(L"APPBOX_TEST_NO_CLEANUP", val))
    {
        appbox::test::config.no_cleanup = _wcsicmp(val.c_str(), L"0") && _wcsicmp(val.c_str(), L"false") &&
                                          _wcsicmp(val.c_str(), L"no") && _wcsicmp(val.c_str(), L"off");
    }
    if (appbox::test::ReadEnvironmentVariable(L"APPBOX_TEST_MODE", val))
    {
        if (!appbox::test::ParseTestMode(val, appbox::test::config.mode))
        {
            std::fprintf(stderr, "AppBox tests: APPBOX_TEST_MODE is not a test mode: %s\n",
                         appbox::WideToUTF8(val).c_str());
        }
    }

    /* The environment is read first, so an option of the command line wins. */
    appbox::test::LoadTestTimeoutFromEnvironment(appbox::test::config.test_timeout);
}

} // namespace

const char* appbox::test::TestModeName(TestMode mode)
{
    switch (mode)
    {
    case TestMode::Unit:
        return "unit";
    case TestMode::EndToEnd:
        return "e2e";
    case TestMode::All:
    default:
        return "all";
    }
}

bool appbox::test::ParseTestMode(const std::wstring& text, TestMode& mode)
{
    if (_wcsicmp(text.c_str(), L"all") == 0)
    {
        mode = TestMode::All;
        return true;
    }
    if (_wcsicmp(text.c_str(), L"unit") == 0)
    {
        mode = TestMode::Unit;
        return true;
    }
    if (_wcsicmp(text.c_str(), L"e2e") == 0)
    {
        mode = TestMode::EndToEnd;
        return true;
    }

    return false;
}

int appbox::test::SetupTestConfig(CLI::App& app)
{
    SetConfigFromEnv();

    app.add_option("--launcher", appbox::test::config.launcher_path,
                   "Path to launcher. Environment variable: APPBOX_TEST_LAUNCHER.");
    app.add_option("--sandbox32", appbox::test::config.sandbox32_path,
                   "Path to the 32 bit sandbox injection module. Environment variable: APPBOX_TEST_SANDBOX32.");
    app.add_option("--sandbox64", appbox::test::config.sandbox64_path,
                   "Path to the 64 bit sandbox injection module. Environment variable: APPBOX_TEST_SANDBOX64.");
    app.add_option("--packer", appbox::test::config.packer_path,
                   "Path to the packer executable. Environment variable: APPBOX_TEST_PACKER.");
    app.add_option_function<std::wstring>("--log-level", SetCaseLogLevelFromString,
                                          "Log level of the launcher and of the sandboxed processes of a case. "
                                          "Available levels: trace, debug, info, warn, err, critical, off. "
                                          "Default: info. Environment variable: APPBOX_TEST_LOG_LEVEL.");
    app.add_option("--no-cleanup", appbox::test::config.no_cleanup,
                   "Do not cleanup the test directory. Environment variable: APPBOX_TEST_NO_CLEANUP.");
    app.add_option_function<std::wstring>("--mode", SetModeFromString,
                                          "Part of the suites the run executes: all, unit or e2e. Default: all. "
                                          "Environment variable: APPBOX_TEST_MODE.");
    app.add_option("--test-timeout", appbox::test::config.test_timeout.test_timeout_seconds,
                   "Timeout of one test case, in seconds. 0 turns the watchdog off. Default: 300. "
                   "Environment variable: APPBOX_TEST_TIMEOUT.")
        ->check(CLI::NonNegativeNumber);
    app.add_option("--test-dump-dir", appbox::test::config.test_timeout.test_dump_dir,
                   "Directory the coredumps of a timed out test case are written to. Default: a `coredump` "
                   "directory below the test executable. Environment variable: APPBOX_TEST_DUMP_DIR.");

    return 0;
}

int appbox::test::ApplyTestModeFilter(TestMode mode)
{
    std::string error;
    if (!CheckSuiteOwnership(error))
    {
        std::fprintf(stderr, "AppBox tests: %s\n", error.c_str());
        SPDLOG_ERROR("{}", error);
        return 1;
    }

    if (mode == TestMode::All)
    {
        return 0;
    }

    FilterParts parts = SplitFilter(GTEST_FLAG_GET(filter));
    if (parts.positive.empty())
    {
        parts.positive = kUniversalFilter;
    }

    /*
     * The mode is the range of the run and the filter of the command line only
     * narrows it: the suites of the other mode are excluded whatever the
     * positive filter names, and GoogleTest applies the negative part of a
     * filter with precedence over the positive one. A run therefore stays
     * inside its mode, and `--mode=e2e --gtest_filter=E2E_Reg.Full_*` still runs
     * the single case it names.
     */
    AppendPattern(parts.negative, ExcludedSuitePattern(mode));

    std::string composed = parts.positive;
    if (!parts.negative.empty())
    {
        composed += kNegationSeparator;
        composed += parts.negative;
    }

    GTEST_FLAG_SET(filter, composed);
    return 0;
}
