#include "probe/EnvironmentRead.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <algorithm>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Env;
using namespace appbox::test;

/**
 * Condition:
 * 1. The host holds `APPBOX_ENV_EXPAND_PREPEND=host`.
 * 2. The isolation file lists variables whose values reference a known folder
 *    of this machine with `%APPBOX:<NAME>%`, in the canonical spelling, with
 *    another case of the prefix and with another case of the name.
 * 3. One of them is merged in front of the value of the host, one references a
 *    name the sandbox does not know, one references a `%NAME%` variable of the
 *    shell and one references the system folder of the machine, which the
 *    preset directories of the packer offer like the folders of the user.
 * 4. The sandboxed application reads every variable.
 *
 * Expected:
 * 1. Every known reference is replaced with the real path of the known folder
 *    of this machine, so the archive stays correct on a machine whose folders
 *    are somewhere else.
 * 2. The merge joins the expanded text with the value of the host.
 * 3. The unknown reference, the reference of the shell and the two references
 *    which are spelled with another case keep their spelling, because the
 *    prefix and the name are compared with the case of the table.
 * 4. The block which enumerates the environment carries the expanded value.
 */
TEST_F(E2E_Env, VariableExpansion)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"environment", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    const std::wstring documents = GetKnownFolderPath(L"#Documents#", false);
    const std::wstring profile = GetKnownFolderPath(L"#USERPROFILE#", false);
    const std::wstring windows = GetKnownFolderPath(L"#Windows#", false);
    const std::wstring programs = GetKnownFolderPath(L"#ProgramFilesX64#", false);
    const std::wstring common_x64 = GetKnownFolderPath(L"#ProgramFilesCommonX64#", false);
    const std::wstring system_x86 = GetKnownFolderPath(L"#SystemX86#", false);

    const HostEnvironmentVariable host(L"APPBOX_ENV_EXPAND_PREPEND", L"host");

    ASSERT_TRUE(WriteEnvironmentIsolationFile(
        GetCWD(), {
                      { L"APPBOX_ENV_EXPAND_DOCUMENTS",   L"%APPBOX:Documents%\\Foo\\Bar",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                            appbox::EnvironmentMergeMode::Replace, L""  },
                      { L"APPBOX_ENV_EXPAND_PREFIX_CASE", L"%appbox:documents%\\Foo",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                            appbox::EnvironmentMergeMode::Replace, L""  },
                      { L"APPBOX_ENV_EXPAND_PROFILE",     L"%APPBOX:USERPROFILE%\\Foo",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                            appbox::EnvironmentMergeMode::Replace, L""  },
                      { L"APPBOX_ENV_EXPAND_PREPEND",     L"%APPBOX:Documents%\\Foo",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                            appbox::EnvironmentMergeMode::Prepend, L";" },
                      { L"APPBOX_ENV_EXPAND_UNKNOWN",     L"%APPBOX:Unknown%\\Foo",               appbox::EnvironmentIsolation::WriteCopy,
                       appbox::EnvironmentMergeMode::Replace,                                                                                                                     L""  },
                      { L"APPBOX_ENV_EXPAND_SHELL",       L"%PATH%",                              appbox::EnvironmentIsolation::WriteCopy,
                       appbox::EnvironmentMergeMode::Replace,                                                                                                                     L""  },
                      { L"APPBOX_ENV_EXPAND_WINDOWS",     L"%APPBOX:Windows%\\Foo",               appbox::EnvironmentIsolation::WriteCopy,
                       appbox::EnvironmentMergeMode::Replace,                                                                                                                     L""  },
                      { L"APPBOX_ENV_EXPAND_PROGRAMS",    L"%APPBOX:ProgramFiles%\\Foo",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                            appbox::EnvironmentMergeMode::Replace, L""  },
                      { L"APPBOX_ENV_EXPAND_PROGRAMS64",  L"%APPBOX:ProgramFilesX64%\\Foo",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                            appbox::EnvironmentMergeMode::Replace, L""  },
                      { L"APPBOX_ENV_EXPAND_COMMON64",    L"%APPBOX:ProgramFilesCommonX64%\\Foo",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                            appbox::EnvironmentMergeMode::Replace, L""  },
                      { L"APPBOX_ENV_EXPAND_SYSTEMX86",   L"%APPBOX:SystemX86%\\Foo",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                            appbox::EnvironmentMergeMode::Replace, L""  },
                      { L"APPBOX_ENV_EXPAND_NAME_CASE",   L"%APPBOX:documents%\\Foo",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                            appbox::EnvironmentMergeMode::Replace, L""  }
    }));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_ENV_EXPAND_DOCUMENTS", "APPBOX_ENV_EXPAND_PREFIX_CASE", "APPBOX_ENV_EXPAND_PROFILE",
                  "APPBOX_ENV_EXPAND_PREPEND",   "APPBOX_ENV_EXPAND_UNKNOWN",     "APPBOX_ENV_EXPAND_SHELL",
                  "APPBOX_ENV_EXPAND_WINDOWS",   "APPBOX_ENV_EXPAND_PROGRAMS",    "APPBOX_ENV_EXPAND_PROGRAMS64",
                  "APPBOX_ENV_EXPAND_COMMON64",  "APPBOX_ENV_EXPAND_SYSTEMX86",   "APPBOX_ENV_EXPAND_NAME_CASE" };

    const auto rsp = ProbeEnvironmentRead.Call(req, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), req.names.size());
    ASSERT_EQ(rsp.ansi_values.size(), req.names.size());

    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], appbox::WideToUTF8(documents + L"\\Foo\\Bar"));

    /*
     * The prefix and the name are compared with the case of the table, so a
     * reference which is spelled with another case keeps its own spelling.
     */
    EXPECT_EQ(rsp.values[1], "%appbox:documents%\\Foo");
    EXPECT_EQ(rsp.values[2], appbox::WideToUTF8(profile + L"\\Foo"));

    /* The value of the host is joined behind the expanded text of the row. */
    EXPECT_EQ(rsp.values[3], appbox::WideToUTF8(documents + L"\\Foo") + ";host");

    EXPECT_EQ(rsp.values[4], "%APPBOX:Unknown%\\Foo");
    EXPECT_EQ(rsp.values[5], "%PATH%");

    /* The system folder of the machine is referenced like the folders of the user. */
    EXPECT_TRUE(rsp.found[6]);
    EXPECT_EQ(rsp.values[6], appbox::WideToUTF8(windows + L"\\Foo"));

    /*
     * The 64 bit process sees the 64 bit program folders, whether the value
     * names the folder through the variable of the process or through the
     * variable of the 64 bit folder.
     */
    EXPECT_TRUE(rsp.found[7]);
    EXPECT_EQ(rsp.values[7], appbox::WideToUTF8(programs + L"\\Foo"));
    EXPECT_TRUE(rsp.found[8]);
    EXPECT_EQ(rsp.values[8], appbox::WideToUTF8(programs + L"\\Foo"));
    EXPECT_TRUE(rsp.found[9]);
    EXPECT_EQ(rsp.values[9], appbox::WideToUTF8(common_x64 + L"\\Foo"));

    /* The 32 bit system folder is reachable from a 64 bit process as well. */
    EXPECT_TRUE(rsp.found[10]);
    EXPECT_EQ(rsp.values[10], appbox::WideToUTF8(system_x86 + L"\\Foo"));

    /* A name which is spelled with another case is kept like an unknown one. */
    EXPECT_EQ(rsp.values[11], "%APPBOX:documents%\\Foo");

    /* The ANSI entry point and the block which enumerates report the same view. */
    EXPECT_EQ(rsp.ansi_values[0], rsp.values[0]);

    const std::string entry = "APPBOX_ENV_EXPAND_DOCUMENTS=" + rsp.values[0];
    EXPECT_NE(std::find(rsp.entries.begin(), rsp.entries.end(), entry), rsp.entries.end());

    ASSERT_TRUE(tree.Verify());
}
