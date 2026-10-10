#include "probe/RegChildProcess.hpp"
#include "probe/RegReadValue.hpp"
#include "probe/RegTwoProcesses.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "SandboxLayout.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <filesystem>
#include <spdlog/fmt/fmt.h>

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

namespace
{

/**
 * @brief Describe every observation of the run for the message of an assertion.
 *
 * The report holds the three directions the case observes: what the child
 * process saw of the parent, what the parent saw of the child while the child
 * was alive, and what a second sandbox process reads back afterwards. A failing
 * assertion therefore names the whole measurement instead of a single value.
 *
 * @param[in] rsp The observations of the run of the two processes.
 * @param[in] parent_again The read of the value of the parent in the second run.
 * @param[in] child_again The read of the value of the child in the second run.
 * @return The description of the run.
 */
std::string FormatReport(const ProtocolRegTwoProcesses::Rsp& rsp, const ProtocolRegReadValue::Rsp& parent_again,
                         const ProtocolRegReadValue::Rsp& child_again)
{
    return fmt::format("parent: create={} disposition={} set={} readback='{}'\n"
                       "child: launch={} exit={} timed_out={}\n"
                       "child observation: {}\n"
                       "parent read of the child: code={} alive_when_read={} data='{}'\n"
                       "parent read of the observation: code={} data='{}'\n"
                       "second run: parent open={} query={} data='{}' | child open={} query={} data='{}'",
                       rsp.create_code, rsp.disposition, rsp.set_code, rsp.parent_readback, rsp.child_launch_code,
                       rsp.child_exit_code, rsp.child_timed_out, DescribeRegChildExitCode(rsp.child_exit_code),
                       rsp.child_value_read_code, rsp.child_alive_when_read, rsp.child_value_readback,
                       rsp.child_saw_read_code, rsp.child_saw_readback, parent_again.open_code, parent_again.query_code,
                       parent_again.data, child_again.open_code, child_again.query_code, child_again.data);
}

} // namespace

/**
 * Condition:
 * 1. A sandboxed probe process writes a value into a key of the sandbox hive.
 * 2. The probe starts a second sandboxed process (the `regchild` worker) which
 *    reads that value and writes a value of its own, then stays alive.
 * 3. The probe reads the value of the child while the child is still running.
 * 4. A second sandbox run reads both values back.
 *
 * Expected:
 * 1. The child process reads the value of the parent, so the two processes of
 *    one sandbox share the registry state while both of them run.
 * 2. The parent process reads the value of the child while the child is alive,
 *    which tells a shared view from a state which is only written back when a
 *    process leaves.
 * 3. The second run reads both values, so neither process loses the state of
 *    the other one when it leaves.
 * 4. The real HKCU does not carry the key, so the whole exchange stays in the
 *    hive of the sandbox.
 */
TEST_F(E2E_Reg, TwoProcesses_ShareTheHive)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\TwoProcesses_" + appbox::UTF8ToWide(appbox::RandomString(8));

    ProtocolRegTwoProcesses::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.ParentValue = "ParentValue";
    req.ParentData = "parent";
    req.ChildValue = "ChildValue";
    req.ChildData = "child";
    req.ChildHoldMs = 2000;

    const auto rsp = ProbeRegTwoProcesses.Call(req, GetCWD(), config).get<ProtocolRegTwoProcesses::Rsp>();

    /* The second run mounts the hive of the state directory of the first run. */
    ProtocolRegReadValue::Req read_parent;
    read_parent.Key = req.Key;
    read_parent.Value = req.ParentValue;
    const auto parent_again = ProbeRegReadValue.Call(read_parent, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();

    ProtocolRegReadValue::Req read_child;
    read_child.Key = req.Key;
    read_child.Value = req.ChildValue;
    const auto child_again = ProbeRegReadValue.Call(read_child, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();

    const std::string report = FormatReport(rsp, parent_again, child_again);

    /* The parent process wrote its value and started the worker. */
    ASSERT_EQ(rsp.create_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    ASSERT_EQ(rsp.set_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    ASSERT_EQ(rsp.parent_readback, req.ParentData) << report;
    ASSERT_EQ(rsp.child_launch_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    ASSERT_FALSE(rsp.child_timed_out) << report;

    /* The child ran, opened the key, read the value of the parent and wrote its own. */
    EXPECT_EQ(rsp.child_exit_code & RegChildOpenedKey, static_cast<DWORD>(RegChildOpenedKey)) << report;
    EXPECT_EQ(rsp.child_exit_code & RegChildReadValue, static_cast<DWORD>(RegChildReadValue)) << report;
    EXPECT_EQ(rsp.child_exit_code & RegChildValueMatched, static_cast<DWORD>(RegChildValueMatched)) << report;
    EXPECT_EQ(rsp.child_exit_code & RegChildWroteValues, static_cast<DWORD>(RegChildWroteValues)) << report;

    /* The parent read the value of the child while the child was alive. */
    EXPECT_EQ(rsp.child_value_read_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    EXPECT_EQ(rsp.child_value_readback, req.ChildData) << report;
    EXPECT_TRUE(rsp.child_alive_when_read) << report;

    /* The observation the child wrote names the value of the parent. */
    EXPECT_EQ(rsp.child_saw_read_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    EXPECT_EQ(rsp.child_saw_readback, req.ParentData) << report;

    /* The state of both processes survives the run. */
    EXPECT_EQ(parent_again.query_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    EXPECT_EQ(parent_again.data, req.ParentData) << report;
    EXPECT_EQ(child_again.query_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    EXPECT_EQ(child_again.data, req.ChildData) << report;

    /* The real registry must not contain the key. */
    {
        HKEY       key = nullptr;
        const auto result = RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key);
        EXPECT_NE(result, ERROR_SUCCESS);
        if (result == ERROR_SUCCESS)
        {
            RegCloseKey(key);
        }
    }

    /* The hive file must exist in the state directory of the sandbox. */
    const auto hive = GetCWD() / appbox::layout::kStateDirNameW / appbox::layout::kRegistryDirNameW /
                      appbox::layout::kRegistryHiveFileNameW;
    EXPECT_TRUE(std::filesystem::exists(hive)) << report;
}
