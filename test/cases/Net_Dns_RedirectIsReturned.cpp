#include "probe/ResolveName.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/NetworkIsolationBuilder.hpp"

typedef appbox::test::CommonFixture Net;
using namespace appbox::test;

/**
 * Condition:
 * 1. The DNS redirections of the network workspace list a hostname which the
 *    host cannot resolve, together with the address it resolves to.
 * 2. The sandboxed application resolves the hostname with the name resolution
 *    of winsock and with the one of the DNS client.
 *
 * Expected:
 * 1. Both calls report success and answer with the redirect address, so the
 *    name was answered from the isolation file instead of being asked at the
 *    host.
 */
TEST_F(Net, Dns_RedirectIsReturned)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {})
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteNetworkIsolationFile(config, {
                                                      { L"appbox-spike.invalid", L"127.0.0.1" }
    }));

    /* The winsock resolution of the application. */
    {
        ProtocolResolveName::Req req;
        req.name = "appbox-spike.invalid";
        req.api = ProtocolResolveName::Req::Api::GetAddrInfo;
        req.family = AF_UNSPEC;

        const auto rsp = ProbeResolveName.Call(req, GetCWD(), config).get<ProtocolResolveName::Rsp>();
        EXPECT_EQ(rsp.code, 0);
        ASSERT_EQ(rsp.addresses.size(), 1u);
        EXPECT_EQ(rsp.addresses[0], "127.0.0.1");
    }

    /* The DNS client of the application. */
    {
        ProtocolResolveName::Req req;
        req.name = "appbox-spike.invalid";
        req.api = ProtocolResolveName::Req::Api::DnsQuery;
        req.type = DNS_TYPE_A;

        const auto rsp = ProbeResolveName.Call(req, GetCWD(), config).get<ProtocolResolveName::Rsp>();
        EXPECT_EQ(rsp.code, 0);
        ASSERT_EQ(rsp.addresses.size(), 1u);
        EXPECT_EQ(rsp.addresses[0], "127.0.0.1");
    }

    ASSERT_TRUE(tree.Verify());
}
