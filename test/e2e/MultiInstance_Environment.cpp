#include "utils/CommonFixture.hpp"
#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/MultiInstance.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"
#include "utils/TestSandboxId.hpp"
#include "WString.hpp"
#include "launcher/Config.hpp"
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_MultiInstance;
using namespace appbox::test;

namespace
{

/** Budget of a case which waits for the programs of a run, in milliseconds. */
constexpr std::uint32_t kStageTimeoutMs = 60000;

/**
 * @brief Budget the master run has to end in once its request is served, in
 *        milliseconds.
 *
 * The case ends the master run itself, so a run which outlives the request by
 * more than the budget waited for a hold instead of for the case.
 */
constexpr ULONGLONG kMasterEndBudgetMs = 3000;

/** Name of the variable the sandbox joins its own value to. */
constexpr const char* kJoinedVariable = "APPBOX_ENV_HOST";

/** Value the host holds for that variable. */
constexpr const wchar_t* kHostValue = L"foo";

/** Value the sandbox joins behind the value of the host. */
constexpr const wchar_t* kSandboxValue = L"bar";

/** Text which joins the two values, so a value joined twice is visible. */
constexpr const wchar_t* kMergeString = L";";

/** Value of a variable which was composed once. */
constexpr const char* kComposedOnce = "foo;bar";

/**
 * @brief Read one variable from the environment a probe reported.
 * @param[in] entries Entries of the environment, one `name=value` text each.
 * @param[in] name Name of the variable.
 * @return The value, empty when the environment does not carry the variable.
 */
std::string EnvironmentValue(const std::vector<std::string>& entries, const std::string& name)
{
    const std::string prefix = name + "=";
    for (const auto& entry : entries)
    {
        if (entry.rfind(prefix, 0) == 0)
        {
            return entry.substr(prefix.size());
        }
    }

    return {};
}

/**
 * @brief Write the isolation file which joins a value to the value of the host.
 * @param[in] root Root directory of the sandbox.
 * @return true on success.
 */
bool WriteJoinedVariable(const std::filesystem::path& root)
{
    return WriteEnvironmentIsolationFile(
        root, {
                  { appbox::UTF8ToWide(kJoinedVariable), kSandboxValue, appbox::EnvironmentIsolation::WriteCopy,
                   appbox::EnvironmentMergeMode::Append, kMergeString }
    });
}

} // namespace

/**
 * Condition:
 * 1. The host holds `APPBOX_ENV_HOST=foo`, the isolation file joins `bar` to it
 *    with `;`, so a sandboxed process sees `foo;bar`.
 * 2. A launcher of the host serves a sandbox and its own program works; the
 *    case ends that run itself once its request is served.
 * 3. A second launcher of the host asks for a program of the sandbox.
 *
 * Expected:
 * 1. The environment the host passes on is not composed yet, so the sandbox of
 *    the created process composes it exactly once: the probe sees `foo;bar` and
 *    not `foo` or `foo;bar;bar`.
 * 2. The run of the master instance ends as soon as the case asks it to, which
 *    is what a run without a fixed hold costs.
 */
TEST_F(E2E_MultiInstance, EnvironmentComposedByTheSandbox)
{
    const auto cwd = GetCWD() / L"host";
    const auto id = SandboxIdForPath(cwd.wstring());

    const HostEnvironmentVariable host(appbox::UTF8ToWide(kJoinedVariable), kHostValue);

    const auto launcher_config = MultiInstanceConfig(cwd, id, "app");
    ASSERT_TRUE(WriteJoinedVariable(cwd));

    /*
     * The case holds the run of the master instance: the programs of the run
     * report to it and stay alive until the case ends them, so the run serves
     * the request below and leaves as soon as the case asks it to.
     */
    StartupRunHandle master(cwd.wstring(), launcher_config);
    ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
        << "the master instance never started its application";

    const auto& master_reports = master.Collect();
    ASSERT_EQ(master_reports.reports.size(), static_cast<std::size_t>(1)) << master_reports.reports.size();

    StartupRun request;
    const auto secondary_exit =
        ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "request", 0, LogRun{}, request);

    /*
     * The request is served, so nothing keeps the master run alive any more:
     * the case ends it through the exit request of its programs and measures
     * how long the run takes to end.
     */
    const ULONGLONG request_done = GetTickCount64();
    master.Exit(0);
    const auto      master_exit = master.Wait();
    const ULONGLONG master_end_ms = GetTickCount64() - request_done;

    EXPECT_EQ(secondary_exit, 0u);
    EXPECT_EQ(master_exit, 0u);
    EXPECT_LT(master_end_ms, kMasterEndBudgetMs) << "the case waited for a fixed hold of the master instance";

    ASSERT_EQ(request.reports.size(), static_cast<std::size_t>(1)) << request.reports.size();
    EXPECT_EQ(EnvironmentValue(request.reports[0].environment, kJoinedVariable), kComposedOnce);
}
