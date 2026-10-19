#include "probe/EnvironmentRead.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Env;
using namespace appbox::test;

/**
 * Condition:
 * 1. The isolation file lists variables whose values reference the folders the
 *    operating system shows to a 32 bit process.
 * 2. The sandboxed application is the 32 bit probe, so the sandbox which expands
 *    the values is the module of a 32 bit process.
 *
 * Expected:
 * 1. `%APPBOX:ProgramFiles%` is `C:\Program Files (x86)` and
 *    `%APPBOX:ProgramFilesCommon%` is `C:\Program Files (x86)\Common Files`,
 *    which are the folders a 32 bit process of the machine sees.
 * 2. `%APPBOX:ProgramFilesX64%` is not supported for a 32 bit process, so the
 *    reference keeps its own spelling, while
 *    `%APPBOX:ProgramFilesCommonX64%` names the 64 bit `Common Files` folder.
 * 3. The two system folders are the same in both views: `System32` is
 *    `%windir%\system32` and `SystemX86` is `%windir%\SysWOW64`.
 */
TEST_F(E2E_Env, VariableExpansion32)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"environment", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    const std::wstring programs_x86 = GetKnownFolderPath(L"#ProgramFilesX86#", false);
    const std::wstring common_x86 = GetKnownFolderPath(L"#ProgramFilesCommonX86#", false);
    const std::wstring common_x64 = GetKnownFolderPath(L"#ProgramFilesCommonX64#", false);
    const std::wstring system32 = GetKnownFolderPath(L"#System32#", false);
    const std::wstring system_x86 = GetKnownFolderPath(L"#SystemX86#", false);

    ASSERT_TRUE(WriteEnvironmentIsolationFile(
        GetCWD(), {
                      { L"APPBOX_ENV32_PROGRAM_FILES",     L"%APPBOX:ProgramFiles%\\Foo",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                             appbox::EnvironmentMergeMode::Replace, L"" },
                      { L"APPBOX_ENV32_PROGRAM_FILES_X64", L"%APPBOX:ProgramFilesX64%\\Foo",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                             appbox::EnvironmentMergeMode::Replace, L"" },
                      { L"APPBOX_ENV32_COMMON",            L"%APPBOX:ProgramFilesCommon%\\Foo",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                             appbox::EnvironmentMergeMode::Replace, L"" },
                      { L"APPBOX_ENV32_COMMON_X64",        L"%APPBOX:ProgramFilesCommonX64%\\Foo",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                             appbox::EnvironmentMergeMode::Replace, L"" },
                      { L"APPBOX_ENV32_SYSTEM32",          L"%APPBOX:System32%\\Foo",              appbox::EnvironmentIsolation::WriteCopy,
                       appbox::EnvironmentMergeMode::Replace,                                                                                                                      L"" },
                      { L"APPBOX_ENV32_SYSTEM_X86",        L"%APPBOX:SystemX86%\\Foo",             appbox::EnvironmentIsolation::WriteCopy,
                       appbox::EnvironmentMergeMode::Replace,                                                                                                                      L"" }
    }));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_ENV32_PROGRAM_FILES", "APPBOX_ENV32_PROGRAM_FILES_X64", "APPBOX_ENV32_COMMON",
                  "APPBOX_ENV32_COMMON_X64",    "APPBOX_ENV32_SYSTEM32",          "APPBOX_ENV32_SYSTEM_X86" };

    const auto rsp =
        ProbeEnvironmentRead.Call(req, GetCWD(), config, ProbeBitness::Bit32).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), req.names.size());
    ASSERT_EQ(rsp.ansi_values.size(), req.names.size());

    for (std::size_t index = 0; index < req.names.size(); ++index)
    {
        EXPECT_TRUE(rsp.found[index]) << req.names[index];
    }

    /* The folders a 32 bit process sees. */
    EXPECT_EQ(rsp.values[0], appbox::WideToUTF8(programs_x86 + L"\\Foo"));
    EXPECT_EQ(rsp.values[2], appbox::WideToUTF8(common_x86 + L"\\Foo"));

    /* A folder which does not exist for a 32 bit process keeps the reference. */
    EXPECT_EQ(rsp.values[1], "%APPBOX:ProgramFilesX64%\\Foo");

    /* The 64 bit `Common Files` folder is reachable from a 32 bit process. */
    EXPECT_EQ(rsp.values[3], appbox::WideToUTF8(common_x64 + L"\\Foo"));

    /* The system folders are the same in both views. */
    EXPECT_EQ(rsp.values[4], appbox::WideToUTF8(system32 + L"\\Foo"));
    EXPECT_EQ(rsp.values[5], appbox::WideToUTF8(system_x86 + L"\\Foo"));

    /* The ANSI entry point and the block which enumerates report the same view. */
    EXPECT_EQ(rsp.ansi_values[0], rsp.values[0]);

    const std::string entry = "APPBOX_ENV32_PROGRAM_FILES=" + rsp.values[0];
    EXPECT_NE(std::find(rsp.entries.begin(), rsp.entries.end(), entry), rsp.entries.end());

    ASSERT_TRUE(tree.Verify());
}
