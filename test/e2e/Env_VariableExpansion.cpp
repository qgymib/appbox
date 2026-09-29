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
 *    of this machine with `%APPBOX:<NAME>%`, in the canonical spelling and in
 *    another one of the prefix and of the name.
 * 3. One of them is merged in front of the value of the host, one references a
 *    name the sandbox does not know and one references a `%NAME%` variable of
 *    the shell.
 * 4. The sandboxed application reads every variable.
 *
 * Expected:
 * 1. Every known reference is replaced with the real path of the known folder
 *    of this machine, so the archive stays correct on a machine whose folders
 *    are somewhere else.
 * 2. The merge joins the expanded text with the value of the host.
 * 3. The unknown reference and the reference of the shell keep their spelling.
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

    const HostEnvironmentVariable host(L"APPBOX_ENV_EXPAND_PREPEND", L"host");

    ASSERT_TRUE(WriteEnvironmentIsolationFile(
        GetCWD(), {
                      { L"APPBOX_ENV_EXPAND_DOCUMENTS", L"%APPBOX:Documents%\\Foo\\Bar",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                   appbox::EnvironmentMergeMode::Replace, L""  },
                      { L"APPBOX_ENV_EXPAND_CASE",      L"%appbox:documents%\\Foo",      appbox::EnvironmentIsolation::WriteCopy,
                       appbox::EnvironmentMergeMode::Replace,                                                                                                            L""  },
                      { L"APPBOX_ENV_EXPAND_PROFILE",   L"%APPBOX:USERPROFILE%\\Foo",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                   appbox::EnvironmentMergeMode::Replace, L""  },
                      { L"APPBOX_ENV_EXPAND_PREPEND",   L"%APPBOX:Documents%\\Foo",
                       appbox::EnvironmentIsolation::WriteCopy,                                                                   appbox::EnvironmentMergeMode::Prepend, L";" },
                      { L"APPBOX_ENV_EXPAND_UNKNOWN",   L"%APPBOX:Unknown%\\Foo",        appbox::EnvironmentIsolation::WriteCopy,
                       appbox::EnvironmentMergeMode::Replace,                                                                                                            L""  },
                      { L"APPBOX_ENV_EXPAND_SHELL",     L"%PATH%",                       appbox::EnvironmentIsolation::WriteCopy,
                       appbox::EnvironmentMergeMode::Replace,                                                                                                            L""  }
    }));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_ENV_EXPAND_DOCUMENTS", "APPBOX_ENV_EXPAND_CASE",    "APPBOX_ENV_EXPAND_PROFILE",
                  "APPBOX_ENV_EXPAND_PREPEND",   "APPBOX_ENV_EXPAND_UNKNOWN", "APPBOX_ENV_EXPAND_SHELL" };

    const auto rsp = ProbeEnvironmentRead.Call(req, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), req.names.size());
    ASSERT_EQ(rsp.ansi_values.size(), req.names.size());

    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], appbox::WideToUTF8(documents + L"\\Foo\\Bar"));
    EXPECT_EQ(rsp.values[1], appbox::WideToUTF8(documents + L"\\Foo"));
    EXPECT_EQ(rsp.values[2], appbox::WideToUTF8(profile + L"\\Foo"));

    /* The value of the host is joined behind the expanded text of the row. */
    EXPECT_EQ(rsp.values[3], appbox::WideToUTF8(documents + L"\\Foo") + ";host");

    EXPECT_EQ(rsp.values[4], "%APPBOX:Unknown%\\Foo");
    EXPECT_EQ(rsp.values[5], "%PATH%");

    /* The ANSI entry point and the block which enumerates report the same view. */
    EXPECT_EQ(rsp.ansi_values[0], rsp.values[0]);

    const std::string entry = "APPBOX_ENV_EXPAND_DOCUMENTS=" + rsp.values[0];
    EXPECT_NE(std::find(rsp.entries.begin(), rsp.entries.end(), entry), rsp.entries.end());

    ASSERT_TRUE(tree.Verify());
}
