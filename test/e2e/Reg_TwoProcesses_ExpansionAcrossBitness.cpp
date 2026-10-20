#include "probe/RegChildProcess.hpp"
#include "probe/RegTwoProcesses.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"
#include "utils/RegChildExitCode.hpp"
#include "utils/TestKnownFolder.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include "tracer/PeImage.hpp"
#include <spdlog/fmt/fmt.h>
#include <string>

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
    const bool is_32_bit_image = appbox::tracer::PeImage::FromFile(ProbePath(bitness)).Is32Bit();
    const bool is_32_bit_probe = bitness == ProbeBitness::Bit32;

    ASSERT_EQ(is_32_bit_image, is_32_bit_probe) << "the probe of the run is not a " << (is_32_bit_probe ? 32 : 64)
                                                << " bit image: pass the probes as --probe32= and --probe64=";
}

/**
 * @brief Run the case of a parent and a child process of the given bit sizes.
 *
 * The parent process writes the value `Path` with the text
 * `%APPBOX:ProgramFiles%\Foo` into a key of the sandbox hive and reads it back,
 * then it starts the worker (`regchild`) of the other bit size, which reads the
 * same value and writes what it read into a value of its own.
 *
 * @param[in] cwd Working directory of the case.
 * @param[in] parent Bit size of the probe process which runs the case.
 * @param[in] child Bit size of the worker process the probe starts.
 */
void ExpectExpansionAcrossBitness(const std::wstring& cwd, ProbeBitness parent, ProbeBitness child)
{
    ExpectProbeImage(parent);
    ExpectProbeImage(child);

    /* clang-format off */
    auto tree = FsRoot(cwd, {
        FsDir(L"data")
    });
    /* clang-format on */

    const auto launcher_config = tree.Build();

    const std::wstring programs_64 = GetKnownFolderPath(L"#ProgramFiles#", false);
    const std::wstring programs_86 = GetKnownFolderPath(L"#ProgramFilesX86#", false);

    const auto subkey = L"Software\\AppBoxTest\\ExpansionAcrossBitness_" + appbox::UTF8ToWide(appbox::RandomString(8));

    const std::wstring expected_parent =
        parent == ProbeBitness::Bit64 ? programs_64 + L"\\Foo" : programs_86 + L"\\Foo";
    const std::wstring expected_child = child == ProbeBitness::Bit64 ? programs_64 + L"\\Foo" : programs_86 + L"\\Foo";

    ProtocolRegTwoProcesses::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.ParentValue = "Path";
    req.ParentData = appbox::WideToUTF8(L"%APPBOX:ProgramFiles%\\Foo");
    req.ChildValue = "ChildValue";
    req.ChildData = "child";
    req.ChildExpect = appbox::WideToUTF8(expected_child);
    req.ChildExecutable = appbox::WideToUTF8(ProbePath(child));
    req.ChildHoldMs = 1000;

    const auto rsp = ProbeRegTwoProcesses.Call(req, cwd, launcher_config, parent).get<ProtocolRegTwoProcesses::Rsp>();

    const std::string report = fmt::format(
        "parent: create={} set={} readback='{}' (expected '{}')\n"
        "child: image='{}' launch={} exit={} timed_out={}\n"
        "child observation: {}\n"
        "parent read of the child: code={} data='{}' (expected '{}')",
        rsp.create_code, rsp.set_code, rsp.parent_readback, appbox::WideToUTF8(expected_parent), req.ChildExecutable,
        rsp.child_launch_code, rsp.child_exit_code, rsp.child_timed_out, DescribeRegChildExitCode(rsp.child_exit_code),
        rsp.child_saw_read_code, rsp.child_saw_readback, appbox::WideToUTF8(expected_child));

    ASSERT_EQ(rsp.create_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    ASSERT_EQ(rsp.set_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    ASSERT_EQ(rsp.child_launch_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;
    ASSERT_FALSE(rsp.child_timed_out) << report;

    /*
     * The parent reads the value with the table of its own bit size: the value
     * of the workspace is expanded while it is read and not while the hive is
     * mounted.
     */
    EXPECT_EQ(rsp.parent_readback, appbox::WideToUTF8(expected_parent)) << report;

    /* The child expands the same value with the table of its own bit size. */
    EXPECT_EQ(rsp.child_exit_code & RegChildValueMatched, static_cast<DWORD>(RegChildValueMatched)) << report;
    EXPECT_EQ(rsp.child_saw_readback, appbox::WideToUTF8(expected_child)) << report;

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
 * 1. The case runs in the 64 bit probe, which starts the 32 bit probe as the
 *    worker process of its own.
 * 2. The parent process writes the value `Path` with the text
 *    `%APPBOX:ProgramFiles%\Foo` and reads it back before it starts the worker.
 * 3. The worker reads the same value while both processes run.
 *
 * Expected:
 * 1. The parent reads `C:\Program Files\Foo`, the folder its own table names
 *    for the reference.
 * 2. The worker reads `C:\Program Files (x86)\Foo` and reports the match
 *    through its exit code, so the child of the other bit size expands the
 *    value with its own table instead of the text of its parent.
 * 3. The real HKCU does not carry the key, so the whole exchange stays in the
 *    hive of the sandbox.
 */
TEST_F(E2E_Reg, TwoProcesses_ExpansionChild32OfParent64)
{
    ExpectExpansionAcrossBitness(GetCWD(), ProbeBitness::Bit64, ProbeBitness::Bit32);
}

/**
 * Condition:
 * 1. The case runs in the 32 bit probe, which starts the 64 bit probe as the
 *    worker process of its own.
 * 2. The parent process writes the value `Path` with the text
 *    `%APPBOX:ProgramFiles%\Foo` and reads it back before it starts the worker.
 * 3. The worker reads the same value while both processes run.
 *
 * Expected:
 * 1. The parent reads `C:\Program Files (x86)\Foo`, the folder its own table
 *    names for the reference.
 * 2. The worker reads `C:\Program Files\Foo` and reports the match through its
 *    exit code, so a 32 bit application which starts a 64 bit program does not
 *    give the child the expansion of the 32 bit view.
 * 3. The real HKCU does not carry the key, so the whole exchange stays in the
 *    hive of the sandbox.
 */
TEST_F(E2E_Reg, TwoProcesses_ExpansionChild64OfParent32)
{
    ExpectExpansionAcrossBitness(GetCWD(), ProbeBitness::Bit32, ProbeBitness::Bit64);
}
