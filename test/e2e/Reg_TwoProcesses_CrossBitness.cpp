#include "probe/RegChildProcess.hpp"
#include "probe/RegTwoProcesses.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbePath.hpp"
#include "utils/RegChildExitCode.hpp"
#include "PeImage.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <spdlog/fmt/fmt.h>

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

namespace
{

/**
 * @brief Check that the probe executable of a bit size is an image of that bit size.
 *
 * A run which was given the wrong file for `--probe32` or `--probe64` would
 * otherwise start two processes of the same bit size and report a green case
 * although the pair the case describes never ran.
 *
 * @param[in] bitness The bit size the probe executable has to carry.
 */
void ExpectProbeImage(ProbeBitness bitness)
{
    const bool is_32_bit_image = appbox::PeImage::FromFile(ProbePath(bitness)).Is32Bit();
    const bool is_32_bit_probe = bitness == ProbeBitness::Bit32;

    ASSERT_EQ(is_32_bit_image, is_32_bit_probe) << "the probe of the run is not a " << (is_32_bit_probe ? 32 : 64)
                                                << " bit image: pass the probes as --probe32= and --probe64=";
}

/**
 * @brief Run the case of two processes whose bit sizes the caller chooses.
 *
 * The parent process writes a value into a key of the sandbox hive, starts the
 * worker (`regchild`) of the requested image as a child process of its own,
 * reads the value of the worker while the worker is alive and waits for it. The
 * child is the process whose sandbox module the parent has to inject: a child
 * of the other bit size is the case in which the module of the target bit size
 * is the other one than the module of the parent.
 *
 * @param[in] cwd Working directory of the case.
 * @param[in] parent Bit size of the probe process which runs the case.
 * @param[in] child Bit size of the worker process the probe starts.
 */
void ExpectChildProcess(const std::wstring& cwd, ProbeBitness parent, ProbeBitness child)
{
    ExpectProbeImage(parent);
    ExpectProbeImage(child);

    /* clang-format off */
    auto tree = FsRoot(cwd, {
        FsDir(L"data")
    });
    /* clang-format on */

    auto launcher_config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\CrossBitness_" + appbox::UTF8ToWide(appbox::RandomString(8));

    ProtocolRegTwoProcesses::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.ParentValue = "ParentValue";
    req.ParentData = "parent";
    req.ChildValue = "ChildValue";
    req.ChildData = "child";
    req.ChildExecutable = appbox::WideToUTF8(ProbePath(child));
    req.ChildHoldMs = 2000;

    const auto rsp = ProbeRegTwoProcesses.Call(req, cwd, launcher_config, parent).get<ProtocolRegTwoProcesses::Rsp>();

    const std::string report =
        fmt::format("parent: create={} disposition={} set={} readback='{}'\n"
                    "child: image='{}' launch={} exit={} timed_out={}\n"
                    "child observation: {}\n"
                    "parent read of the child: code={} alive_when_read={} data='{}'\n"
                    "parent read of the observation: code={} data='{}'",
                    rsp.create_code, rsp.disposition, rsp.set_code, rsp.parent_readback, req.ChildExecutable,
                    rsp.child_launch_code, rsp.child_exit_code, rsp.child_timed_out,
                    DescribeRegChildExitCode(rsp.child_exit_code), rsp.child_value_read_code, rsp.child_alive_when_read,
                    rsp.child_value_readback, rsp.child_saw_read_code, rsp.child_saw_readback);

    /* The parent process wrote its value and started the worker. */
    ASSERT_EQ(rsp.create_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    ASSERT_EQ(rsp.set_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    ASSERT_EQ(rsp.parent_readback, req.ParentData) << report;
    ASSERT_EQ(rsp.child_launch_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    ASSERT_FALSE(rsp.child_timed_out) << report;

    /* The worker ran, opened the key, read the value of the parent and wrote its own. */
    EXPECT_EQ(rsp.child_exit_code & RegChildOpenedKey, static_cast<DWORD>(RegChildOpenedKey)) << report;
    EXPECT_EQ(rsp.child_exit_code & RegChildReadValue, static_cast<DWORD>(RegChildReadValue)) << report;
    EXPECT_EQ(rsp.child_exit_code & RegChildValueMatched, static_cast<DWORD>(RegChildValueMatched)) << report;
    EXPECT_EQ(rsp.child_exit_code & RegChildWroteValues, static_cast<DWORD>(RegChildWroteValues)) << report;

    /* The parent read the value of the worker while the worker was alive. */
    EXPECT_EQ(rsp.child_value_read_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    EXPECT_EQ(rsp.child_value_readback, req.ChildData) << report;
    EXPECT_TRUE(rsp.child_alive_when_read) << report;

    /* The observation the worker wrote names the value of the parent. */
    EXPECT_EQ(rsp.child_saw_read_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    EXPECT_EQ(rsp.child_saw_readback, req.ParentData) << report;

    /* The real registry must not contain the key, so the exchange stayed in the sandbox. */
    {
        HKEY       key = nullptr;
        const auto result = RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key);
        EXPECT_NE(result, ERROR_SUCCESS) << report;
        if (result == ERROR_SUCCESS)
        {
            RegCloseKey(key);
        }
    }
}

} // namespace

/**
 * Condition:
 * 1. The case runs in the 32 bit probe, which starts the 64 bit probe as the
 *    worker process of its own.
 * 2. The parent process writes a value into a key of the sandbox hive before it
 *    starts the worker.
 * 3. The worker reads the value of the parent, writes a value of its own and
 *    stays alive while the parent reads that value back.
 *
 * Expected:
 * 1. The sandbox injects the 64 bit module into the child of the 32 bit
 *    process: the worker opens the key of the sandbox hive instead of failing
 *    to start or to reach its own code.
 * 2. The two processes of the one sandbox share the registry state while both
 *    of them run, so the child sees the value of the parent and the parent sees
 *    the value of the child.
 * 3. The real HKCU does not carry the key, so the whole exchange stays in the
 *    hive of the sandbox.
 */
TEST_F(E2E_Reg, TwoProcesses_Child64OfParent32)
{
    ExpectChildProcess(GetCWD(), ProbeBitness::Bit32, ProbeBitness::Bit64);
}

/**
 * Condition:
 * 1. The case runs in the 32 bit probe, which starts the 32 bit probe as the
 *    worker process of its own, so both processes carry the same bit size.
 * 2. The parent writes a value of the sandbox hive before it starts the worker.
 *
 * Expected:
 * 1. The child of the same bit size is injected with the module of that bit
 *    size, which is the path every child of a sandbox took before the case of
 *    the other bit size existed.
 * 2. The two processes share the registry state of the sandbox and the real
 *    HKCU stays untouched.
 */
TEST_F(E2E_Reg, TwoProcesses_Child32OfParent32)
{
    ExpectChildProcess(GetCWD(), ProbeBitness::Bit32, ProbeBitness::Bit32);
}

/**
 * Condition:
 * 1. The case runs in the 64 bit probe, which starts the 32 bit probe as the
 *    worker process of its own.
 * 2. The parent writes a value of the sandbox hive before it starts the worker.
 *
 * Expected:
 * 1. The sandbox injects the 32 bit module into the child of the 64 bit
 *    process, which is the direction of the two bit sizes that the suite
 *    covered before this case.
 * 2. The two processes share the registry state of the sandbox and the real
 *    HKCU stays untouched.
 */
TEST_F(E2E_Reg, TwoProcesses_Child32OfParent64)
{
    ExpectChildProcess(GetCWD(), ProbeBitness::Bit64, ProbeBitness::Bit32);
}
