#include "probe/ResolveName.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/NetworkIsolationBuilder.hpp"

typedef appbox::test::CommonFixture Net;
using namespace appbox::test;

/**
 * Condition:
 * 1. The DNS redirections list a hostname in a mixed case with a trailing dot,
 *    which is the root label of the name.
 * 2. The sandboxed application asks for the name in another case, with and
 *    without the trailing dot.
 *
 * Expected:
 * 1. Every question is answered from the isolation file, because the name
 *    resolution of the host ignores the case and the trailing dot as well.
 */
TEST_F(Net, Dns_HostnameIsNormalized)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {})
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteNetworkIsolationFile(config, {
                                                      { L"Update.Example.COM.", L"127.0.0.1" }
    }));

    for (const auto* name : { "update.example.com", "UPDATE.EXAMPLE.COM.", "Update.Example.Com" })
    {
        ProtocolResolveName::Req req;
        req.name = name;
        req.api = ProtocolResolveName::Req::Api::GetAddrInfo;
        req.family = AF_UNSPEC;

        const auto rsp = ProbeResolveName.Call(req, GetCWD(), config).get<ProtocolResolveName::Rsp>();
        EXPECT_EQ(rsp.code, 0) << name;
        ASSERT_EQ(rsp.addresses.size(), 1u) << name;
        EXPECT_EQ(rsp.addresses[0], "127.0.0.1") << name;
    }

    ASSERT_TRUE(tree.Verify());
}
