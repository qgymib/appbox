#include "probe/ResolveName.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/NetworkIsolationBuilder.hpp"

typedef appbox::test::CommonFixture E2E_Net;
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
TEST_F(E2E_Net, Dns_RedirectIsReturned)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"network", {}) })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteNetworkIsolationFile(GetCWD(), {
                                                        { L"appbox-spike.invalid", L"127.0.0.1" }
    }));

    ProtocolResolveName::Req req;
    /* clang-format off */
    req.questions = {
        { "appbox-spike.invalid", ProtocolResolveName::Api::GetAddrInfo, AF_UNSPEC, DNS_TYPE_A },
        { "appbox-spike.invalid", ProtocolResolveName::Api::DnsQuery,    AF_UNSPEC, DNS_TYPE_A },
    };
    /* clang-format on */

    const auto rsp = ProbeResolveName.Call(req, GetCWD(), config).get<ProtocolResolveName::Rsp>();
    ASSERT_EQ(rsp.answers.size(), 2u);

    /* The winsock resolution of the application. */
    EXPECT_EQ(rsp.answers[0].code, 0);
    ASSERT_EQ(rsp.answers[0].addresses.size(), 1u);
    EXPECT_EQ(rsp.answers[0].addresses[0], "127.0.0.1");

    /* The DNS client of the application. */
    EXPECT_EQ(rsp.answers[1].code, 0);
    ASSERT_EQ(rsp.answers[1].addresses.size(), 1u);
    EXPECT_EQ(rsp.answers[1].addresses[0], "127.0.0.1");

    ASSERT_TRUE(tree.Verify());
}
