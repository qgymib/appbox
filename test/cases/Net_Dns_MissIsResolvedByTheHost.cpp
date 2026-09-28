#include "probe/ResolveName.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/NetworkIsolationBuilder.hpp"

typedef appbox::test::CommonFixture Net;
using namespace appbox::test;

/**
 * Condition:
 * 1. The DNS redirections of the network workspace list one hostname which is
 *    not the one the application asks for.
 * 2. The sandboxed application resolves the name of the host itself, which the
 *    host answers without asking a server.
 *
 * Expected:
 * 1. The call succeeds with the address of the host, so a name which the
 *    isolation file does not list keeps the resolution of the host.
 */
TEST_F(Net, Dns_MissIsResolvedByTheHost)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {})
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteNetworkIsolationFile(config, {
                                                      { L"appbox-spike.invalid", L"10.9.9.9" }
    }));

    ProtocolResolveName::Req req;
    req.name = "localhost";
    req.api = ProtocolResolveName::Req::Api::GetAddrInfo;
    req.family = AF_INET;

    const auto rsp = ProbeResolveName.Call(req, GetCWD(), config).get<ProtocolResolveName::Rsp>();
    EXPECT_EQ(rsp.code, 0);
    ASSERT_FALSE(rsp.addresses.empty());
    EXPECT_EQ(rsp.addresses[0], "127.0.0.1");
    EXPECT_NE(rsp.addresses[0], "10.9.9.9");

    ASSERT_TRUE(tree.Verify());
}
