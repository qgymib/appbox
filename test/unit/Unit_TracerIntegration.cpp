#include <gtest/gtest.h>
#include "tracer/ArmPlan.hpp"
#include "tracer/CdbLocator.hpp"
#include "tracer/CdbSession.hpp"
#include "tracer/TargetProgram.hpp"
#include "tracer/TracedModules.hpp"
#include <algorithm>
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
    std::filesystem::path debugger;             ///< Debugger to use; empty when it was not found.
    std::filesystem::path target;               ///< cmd.exe; empty when it was not found.
    std::vector<appbox::tracer::ArmGroup> plan; ///< Breakpoints of the category scope.
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
    const auto modules = appbox::tracer::LoadTracedModules(directory);
    setup.plan =
        appbox::tracer::BuildArmPlan(modules, appbox::tracer::AllCategories(), false, directory);
    return setup;
}

/**
 * @brief Run cmd.exe with the given arguments below the debugger.
 *
 * @param[in] setup Prepared run.
 * @param[in] arguments Arguments of cmd.exe.
 * @return The result of the run.
 */
appbox::tracer::TraceResult RunCmd(const IntegrationSetup& setup,
                                   const std::vector<std::wstring>& arguments)
{
    appbox::tracer::TraceRequest request;
    request.debugger = setup.debugger;
    request.program = setup.target;
    request.program_args = arguments;
    request.plan = setup.plan;
    request.timeout_seconds = kRunTimeoutSeconds;
    request.stall_timeout_seconds = kStallTimeoutSeconds;
    return appbox::tracer::RunTraceSession(request);
}

} // namespace

/**
 * @brief A run of cmd.exe below the real debugger reports the functions of the
 *        isolation domains which cmd.exe used.
 */
TEST(TracerIntegration, ASingleProcessRunIsTraced)
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

    const auto result = RunCmd(setup, {L"/c", L"echo", L"hi"});

    EXPECT_EQ(result.status, appbox::tracer::RunStatus::Completed) << result.message;
    EXPECT_EQ(result.processes, 1U);
    EXPECT_GE(result.breakpoints, 100U);
    EXPECT_FALSE(result.names.empty());

    /* The scope is armed per process, and the file APIs of the startup are
     * reported. */
    ASSERT_FALSE(result.calls_per_process.empty());
    EXPECT_GT(result.calls_per_process[0], 0U);
    EXPECT_TRUE(HasAnyName(result.names, {L"kernel32!CreateFileW", L"kernelbase!CreateFileW"}));
}

/**
 * @brief A child process of the program is traced as well.
 *
 * A child process is a debugger session of its own which does not inherit the
 * breakpoints of its parent, so the tracer has to arm it again with the module
 * base addresses of that process. The test proves that this happened: both
 * processes reported calls, and the child is a process of its own.
 */
TEST(TracerIntegration, AChildProcessIsTracedAsWell)
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

    const auto result = RunCmd(setup, {L"/c", L"cmd.exe", L"/c", L"echo", L"child"});

    EXPECT_EQ(result.status, appbox::tracer::RunStatus::Completed) << result.message;
    EXPECT_GE(result.breakpoints, 100U);
    ASSERT_GE(result.processes, 2U) << "the child process was not traced";

    /* Both processes produced calls of their own: the child session was armed
     * with the base addresses of the child. */
    ASSERT_GE(result.calls_per_process.size(), 2U);
    EXPECT_GT(result.calls_per_process[0], 0U);
    EXPECT_GT(result.calls_per_process[1], 0U);

    /* Known functions of the three isolation domains are reported. */
    EXPECT_TRUE(HasAnyName(result.names, {L"kernel32!CreateFileW", L"kernelbase!CreateFileW"}));
    EXPECT_TRUE(HasAnyName(result.names,
                           {L"ntdll!NtOpenKey",
                            L"ntdll!NtCreateKey",
                            L"ntdll!NtQueryValueKey",
                            L"kernelbase!RegOpenKeyExW",
                            L"kernelbase!RegQueryValueExW"}));

    /* The aliases of one address are reported together. */
    EXPECT_TRUE(HasName(result.names, L"ntdll!NtClose"));
    EXPECT_TRUE(HasName(result.names, L"ntdll!ZwClose"));
}
