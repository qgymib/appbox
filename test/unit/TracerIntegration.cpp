#include <gtest/gtest.h>
#include "src/core/TracerModel.hpp"
#include "tracer/ArmPlan.hpp"
#include "tracer/CdbLocator.hpp"
#include "tracer/CdbSession.hpp"
#include "tracer/TargetProgram.hpp"
#include "tracer/TracedModules.hpp"
#include "utils/Coredump.hpp"
#include "utils/NameResolutionProbe.hpp"
#include "utils/TestTimeout.hpp"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

/** Hard limit of one integration run, in seconds. */
constexpr unsigned kRunTimeoutSeconds = 300;

/** Seconds the debugger may stay stopped before the run is aborted. */
constexpr unsigned kStallTimeoutSeconds = 60;

/**
 * @brief Report whether a name was collected.
 *
 * @param[in] names Collected names.
 * @param[in] name `module!function` name to look for.
 * @return Whether the name is part of the result.
 */
bool HasName(const std::vector<std::wstring>& names, const std::wstring& name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

/**
 * @brief Report whether one of the names was collected.
 *
 * @param[in] names Collected names.
 * @param[in] candidates Names to look for.
 * @return Whether one of them is part of the result.
 */
bool HasAnyName(const std::vector<std::wstring>& names, const std::vector<std::wstring>& candidates)
{
    for (const auto& candidate : candidates)
    {
        if (HasName(names, candidate))
        {
            return true;
        }
    }

    return false;
}

/** Everything a run of the integration tests needs. */
struct IntegrationSetup
{
    std::filesystem::path                 debugger; ///< Debugger to use; empty when it was not found.
    std::filesystem::path                 target;   ///< cmd.exe; empty when it was not found.
    std::vector<appbox::tracer::ArmGroup> plan;     ///< Breakpoints of the category scope.
};

/**
 * @brief Prepare a run of cmd.exe.
 *
 * @return The setup; an empty debugger or target means that the test has to be
 *         skipped, which keeps the suite usable on a machine without the SDK
 *         debuggers.
 */
IntegrationSetup Prepare()
{
    IntegrationSetup setup;
    setup.debugger = appbox::tracer::ResolveCdb(std::filesystem::path());
    setup.target = appbox::tracer::ResolveTargetProgram(L"cmd.exe");
    if (setup.debugger.empty() || setup.target.empty())
    {
        return setup;
    }

    const std::filesystem::path directory = appbox::tracer::SystemDirectoryForMachine(0);
    const auto                  modules = appbox::tracer::LoadTracedModules(directory);
    setup.plan = appbox::tracer::BuildArmPlan(modules, appbox::tracer::AllCategories(), false, directory);
    return setup;
}

/**
 * @brief Run a program with the given arguments below the debugger.
 *
 * @param[in] setup Prepared run.
 * @param[in] program Program to run.
 * @param[in] arguments Arguments of the program.
 * @return The result of the run.
 */
appbox::tracer::TraceResult RunProgram(const IntegrationSetup& setup, const std::filesystem::path& program,
                                       const std::vector<std::wstring>& arguments)
{
    /*
     * The run below the debugger has a budget of its own which is longer than
     * the timeout of a test case, so the watchdog of the test run has to wait
     * for the debugger instead of stopping the test in the middle of its work.
     */
    appbox::test::SetTestTimeout(static_cast<int>(kRunTimeoutSeconds + kStallTimeoutSeconds + 60));

    appbox::tracer::TraceRequest request;
    request.debugger = setup.debugger;
    request.program = program;
    request.program_args = arguments;
    request.plan = setup.plan;
    request.timeout_seconds = kRunTimeoutSeconds;
    request.stall_timeout_seconds = kStallTimeoutSeconds;
    return appbox::tracer::RunTraceSession(request);
}

/**
 * @brief Run cmd.exe with the given arguments below the debugger.
 *
 * @param[in] setup Prepared run.
 * @param[in] arguments Arguments of cmd.exe.
 * @return The result of the run.
 */
appbox::tracer::TraceResult RunCmd(const IntegrationSetup& setup, const std::vector<std::wstring>& arguments)
{
    return RunProgram(setup, setup.target, arguments);
}

} // namespace

/**
 * @brief A run of cmd.exe below the real debugger reports the lowest level
 *        entry points of the isolation domains which cmd.exe used.
 */
TEST(Unit_TracerIntegration, ASingleProcessRunIsTraced)
{
    const IntegrationSetup setup = Prepare();
    if (setup.debugger.empty())
    {
        GTEST_SKIP() << "cdb.exe was not found";
    }

    if (setup.target.empty())
    {
        GTEST_SKIP() << "cmd.exe was not found";
    }

    ASSERT_FALSE(setup.plan.empty());

    const auto result = RunCmd(setup, { L"/c", L"echo", L"hi" });

    EXPECT_EQ(result.status, appbox::tracer::RunStatus::Completed) << result.message;
    EXPECT_EQ(result.processes, 1U);
    EXPECT_GE(result.breakpoints, 90U);
    EXPECT_FALSE(result.names.empty());

    /* The scope is armed per process, and the file APIs of the startup are
     * reported through their NT entry points. */
    ASSERT_FALSE(result.calls_per_process.empty());
    EXPECT_GT(result.calls_per_process[0], 0U);
    EXPECT_TRUE(HasAnyName(result.names, { L"ntdll!NtCreateFile", L"ntdll!NtOpenFile" }));

    /* The Win32 wrappers are not part of the default scope. */
    EXPECT_FALSE(HasAnyName(result.names, { L"kernel32!CreateFileW", L"kernelbase!CreateFileW" }));
}

/**
 * @brief A child process of the program is traced as well.
 *
 * A child process is a debugger session of its own which does not inherit the
 * breakpoints of its parent, so the tracer has to arm it again with the module
 * base addresses of that process. The test proves that this happened: both
 * processes reported calls, and the child is a process of its own.
 */
TEST(Unit_TracerIntegration, AChildProcessIsTracedAsWell)
{
    const IntegrationSetup setup = Prepare();
    if (setup.debugger.empty())
    {
        GTEST_SKIP() << "cdb.exe was not found";
    }

    if (setup.target.empty())
    {
        GTEST_SKIP() << "cmd.exe was not found";
    }

    ASSERT_FALSE(setup.plan.empty());

    const auto result = RunCmd(setup, { L"/c", L"cmd.exe", L"/c", L"echo", L"child" });

    EXPECT_EQ(result.status, appbox::tracer::RunStatus::Completed) << result.message;
    EXPECT_GE(result.breakpoints, 90U);
    ASSERT_GE(result.processes, 2U) << "the child process was not traced";

    /* Both processes produced calls of their own: the child session was armed
     * with the base addresses of the child. */
    ASSERT_GE(result.calls_per_process.size(), 2U);
    EXPECT_GT(result.calls_per_process[0], 0U);
    EXPECT_GT(result.calls_per_process[1], 0U);

    /* Known entry points of the isolation domains are reported. */
    EXPECT_TRUE(HasAnyName(result.names, { L"ntdll!NtCreateFile", L"ntdll!NtOpenFile" }));
    EXPECT_TRUE(HasAnyName(result.names, { L"ntdll!NtOpenKey", L"ntdll!NtCreateKey", L"ntdll!NtQueryValueKey",
                                           L"ntdll!NtEnumerateValueKey" }));

    /* The aliases of one address are reported together. */
    EXPECT_TRUE(HasName(result.names, L"ntdll!NtClose"));
    EXPECT_TRUE(HasName(result.names, L"ntdll!ZwClose"));
}

/**
 * @brief A module which is loaded on demand is armed when the loader maps it.
 *
 * The DNS client is not part of the import table of a process, so its
 * breakpoints can not be armed at the first prompt of a session: the session
 * has to stop when the module appears, which is what the module load filter of
 * the debugger is for. The case traces the name resolution probe of this
 * executable, which loads the socket library and the DNS client and calls their
 * entry points on demand.
 */
TEST(Unit_TracerIntegration, AModuleLoadedOnDemandIsArmedWhenItAppears)
{
    const IntegrationSetup setup = Prepare();
    if (setup.debugger.empty())
    {
        GTEST_SKIP() << "cdb.exe was not found";
    }

    ASSERT_FALSE(setup.plan.empty());

    const auto result =
        RunProgram(setup, appbox::test::GetOwnExecutablePath(), { appbox::test::kNameResolutionProbeOption });

    EXPECT_EQ(result.status, appbox::tracer::RunStatus::Completed) << result.message;
    EXPECT_EQ(result.processes, 1U);

    /* The socket library is loaded before the initial break of the process, the
     * DNS client only when the probe asks for it. */
    EXPECT_TRUE(HasName(result.names, L"ws2_32!GetAddrInfoW"));
    EXPECT_TRUE(HasName(result.names, L"dnsapi!DnsQuery_UTF8"));
}

/**
 * @brief The workspace marks exactly the rows of its view which a real run
 *        reported, and the summary describes the run.
 *
 * The invariant which makes the black rows meaningful is that a name a run
 * reports is a row of the view: both are built from the same breakpoint plan, so
 * the view can never miss a name the run collected.
 */
TEST(Unit_TracerIntegration, TheViewOfARealRunIsMarked)
{
    const IntegrationSetup setup = Prepare();
    if (setup.debugger.empty())
    {
        GTEST_SKIP() << "cdb.exe was not found";
    }

    if (setup.target.empty())
    {
        GTEST_SKIP() << "cmd.exe was not found";
    }

    const std::filesystem::path      directory = appbox::tracer::SystemDirectoryForMachine(0);
    std::vector<appbox::TracerEntry> entries;
    std::string                      error;
    ASSERT_TRUE(appbox::BuildTracerView(appbox::TracerView::Scope, directory, entries, error)) << error;
    ASSERT_FALSE(entries.empty());

    appbox::test::SetTestTimeout(static_cast<int>(kRunTimeoutSeconds + kStallTimeoutSeconds + 60));

    const appbox::TracerRunOutcome outcome =
        appbox::RunTracerSession(setup.target, { L"/c", L"echo", L"hi" }, appbox::TracerView::Scope, directory, {});
    EXPECT_TRUE(outcome.error.empty()) << outcome.error;

    const appbox::tracer::TraceResult& result = outcome.result;
    ASSERT_EQ(result.status, appbox::tracer::RunStatus::Completed) << result.message;
    ASSERT_FALSE(result.names.empty());

    /* Every name the run collected is a row of the view. */
    for (const auto& name : result.names)
    {
        const bool found = std::any_of(entries.begin(), entries.end(),
                                       [&name](const appbox::TracerEntry& entry) { return entry.name == name; });
        EXPECT_TRUE(found) << name;
    }

    appbox::MarkTracerEntries(entries, result.names);
    appbox::OrderTracerEntries(entries);

    const auto by_name = [](const appbox::TracerEntry& left, const appbox::TracerEntry& right) {
        return left.name < right.name;
    };

    std::size_t used_count = 0;
    for (const auto& entry : entries)
    {
        if (entry.used)
        {
            ++used_count;
        }
    }
    EXPECT_GT(used_count, 0U);

    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        EXPECT_EQ(entries[index].used, index < used_count) << entries[index].name;
    }

    const auto used_end = entries.begin() + static_cast<std::ptrdiff_t>(used_count);
    EXPECT_TRUE(std::is_sorted(entries.begin(), used_end, by_name));
    EXPECT_TRUE(std::is_sorted(used_end, entries.end(), by_name));

    /* The file APIs of the startup are among the used rows. */
    const bool file_api_used = std::any_of(entries.begin(), used_end, [](const appbox::TracerEntry& entry) {
        return entry.name == L"ntdll!NtCreateFile" || entry.name == L"ntdll!NtOpenFile";
    });
    EXPECT_TRUE(file_api_used);

    EXPECT_EQ(appbox::TracerRunSummary(result).rfind(L"processes: 1, ", 0), 0U);
}
