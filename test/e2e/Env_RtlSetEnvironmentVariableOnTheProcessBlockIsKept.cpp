#include "probe/EnvironmentRead.hpp"
#include "probe/EnvironmentWrite.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/FsBuilder.hpp"
#include <string>

typedef appbox::test::CommonFixture E2E_Env;
using namespace appbox::test;

/**
 * Condition:
 * 1. The host holds `APPBOX_ENV_HOST=foo` and the isolation file lists the
 *    variable with the value `bar`.
 * 2. A first run stores the variable through `RtlSetEnvironmentVariable` with
 *    the address of the environment pointer of the process parameters, which is
 *    the way a caller names the environment of the process without a null
 *    block. The counted entry point of `ntdll` treats the two spellings alike:
 *    it compares the block against that pointer and takes the lock of the
 *    process block when the two match.
 * 3. A second run reads the variable.
 *
 * Expected:
 * 1. The write is accepted by the operating system.
 * 2. The second run sees `changed`: the counted entry point named the
 *    environment of the process, so the sandbox recorded the modification and
 *    the next run applies it on top of its layers, exactly like a write of the
 *    Win32 entry point.
 * 3. The environment of the host still carries `foo`.
 *
 * The case does not read the value back in the first run. `RtlSetEnvironmentVar`
 * of `ntdll` updates the size of the environment of the process parameters only
 * while the caller passes a null block; a caller which passes the address of the
 * pointer moves the block and leaves the size of the previous block behind, so
 * the readers of the same process answer from the previous block. The
 * modification is part of the block and of the state either way, which is what
 * the second run pins.
 */
TEST_F(E2E_Env, RtlSetEnvironmentVariableOnTheProcessBlockIsKept)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"environment", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    const HostEnvironmentVariable host(L"APPBOX_ENV_HOST", L"foo");

    ASSERT_TRUE(WriteEnvironmentIsolationFile(GetCWD(),
                                              {
                                                  { L"APPBOX_ENV_HOST", L"bar", appbox::EnvironmentIsolation::WriteCopy,
                                                   appbox::EnvironmentMergeMode::Replace, L"" }
    }));

    ProtocolEnvironmentWrite::Req write;
    write.entry = ProtocolEnvironmentWrite::Entry::RtlBlock;
    write.names = { "APPBOX_ENV_HOST" };
    write.values = { "changed" };

    const auto written = Probe(GetCWD(), config).Call<ProtocolEnvironmentWrite>(write);
    ASSERT_EQ(written.stored.size(), 1u);
    EXPECT_TRUE(written.stored[0]);

    ProtocolEnvironmentRead::Req read;
    read.names = { "APPBOX_ENV_HOST" };

    const auto rsp = Probe(GetCWD(), config).Call<ProtocolEnvironmentRead>(read);
    ASSERT_EQ(rsp.values.size(), 1u);
    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "changed");

    EXPECT_EQ(host.Value(), L"foo");

    ASSERT_TRUE(tree.Verify());
}
