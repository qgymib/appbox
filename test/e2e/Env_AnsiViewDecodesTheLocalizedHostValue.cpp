#include "WinAPI.h" /* Must be first include file */
#include "probe/EnvironmentRead.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/FsBuilder.hpp"
#include <algorithm>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Env;
using namespace appbox::test;

/**
 * Condition:
 * 1. The host holds a variable whose value carries a text the code page of the
 *    machine spells with bytes which are no legal UTF-8 (a Chinese value on a
 *    machine of code page 936, for example), and the isolation file lists no
 *    entry for the variable, so the value of the host passes through.
 * 2. The sandboxed application reads the variable with every view of the
 *    environment the probe answers.
 *
 * Expected:
 * 1. The answer of the probe reaches the test: every text the answer carries
 *    is UTF-8, whatever the bytes of the ANSI view of the host were.
 * 2. The wide view reports the value of the host.
 * 3. On a machine whose code page spells the value, the ANSI view reports the
 *    same text as the wide one; a code page which cannot spell the value
 *    substitutes it, and the two views differ by that substitution.
 */
TEST_F(E2E_Env, AnsiViewDecodesTheLocalizedHostValue)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"environment", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    ASSERT_TRUE(WriteEnvironmentIsolationFile(GetCWD(),
                                              {
                                                  { L"APPBOX_ENV_UNRELATED", L"bar", appbox::EnvironmentIsolation::Full,
                                                   appbox::EnvironmentMergeMode::Replace, L"" }
    }));

    /*
     * The first character of the value is one the code page of a localized
     * machine spells with bytes which are no legal UTF-8: the code page 936
     * spells it with a lead byte a UTF-8 reader refuses, which is the byte
     * which ended the probe before its answer was converted at the source.
     */
    const std::wstring value = L"\x6D4B"
                               L"bar";

    const HostEnvironmentVariable host(L"APPBOX_ENV_TEXT", value);

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_ENV_TEXT" };
    req.expand = { "%APPBOX_ENV_TEXT%" };

    const auto rsp = Probe(GetCWD(), config).Call<ProtocolEnvironmentRead>(req);
    ASSERT_EQ(rsp.values.size(), 1u);
    ASSERT_EQ(rsp.found.size(), 1u);
    ASSERT_EQ(rsp.ansi_values.size(), 1u);
    ASSERT_EQ(rsp.expanded.size(), 1u);
    ASSERT_EQ(rsp.ansi_expanded.size(), 1u);

    /* The wide view of the process reports the value of the host. */
    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "\xE6\xB5\x8B"
                             "bar");
    EXPECT_EQ(rsp.expanded[0], "\xE6\xB5\x8B"
                               "bar");

    const std::string entry = "APPBOX_ENV_TEXT=\xE6\xB5\x8B"
                              "bar";
    EXPECT_NE(std::find(rsp.entries.begin(), rsp.entries.end(), entry), rsp.entries.end());

    /*
     * Whether the code page of the machine spells the value decides what the
     * ANSI view can report: a code page which cannot spell it substitutes the
     * text it has no bytes for, so the equality of the two views is asked from
     * the code page itself, and it holds on every machine which spells the
     * value.
     */
    const int projected = ::WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    ASSERT_GT(projected, 0);
    std::vector<char> ansi(static_cast<std::size_t>(projected), '\0');
    ASSERT_EQ(::WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, ansi.data(), projected, nullptr, nullptr), projected);

    const int restored = ::MultiByteToWideChar(CP_ACP, 0, ansi.data(), -1, nullptr, 0);
    ASSERT_GT(restored, 0);
    std::vector<wchar_t> spelled(static_cast<std::size_t>(restored), L'\0');
    ASSERT_EQ(::MultiByteToWideChar(CP_ACP, 0, ansi.data(), -1, spelled.data(), restored), restored);

    if (std::wstring(spelled.data()) == value)
    {
        EXPECT_EQ(rsp.ansi_values[0], rsp.values[0]);
        EXPECT_EQ(rsp.ansi_expanded[0], rsp.expanded[0]);
        EXPECT_NE(std::find(rsp.ansi_entries.begin(), rsp.ansi_entries.end(), entry), rsp.ansi_entries.end());
    }

    ASSERT_TRUE(tree.Verify());
}
