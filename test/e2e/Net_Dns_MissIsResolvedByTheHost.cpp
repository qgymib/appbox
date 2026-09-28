#include "probe/ResolveName.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/NetworkIsolationBuilder.hpp"

typedef appbox::test::CommonFixture E2E_Net;
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
TEST_F(E2E_Net, Dns_MissIsResolvedByTheHost)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"network", {}) })
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteNetworkIsolationFile(GetCWD(), {
                                                        { L"appbox-spike.invalid", L"10.9.9.9" }
    }));

    ProtocolResolveName::Req req;
    /* clang-format off */
    req.questions = {
        { "localhost", ProtocolResolveName::Api::GetAddrInfo, AF_INET, DNS_TYPE_A },
    };
    /* clang-format on */

    const auto rsp = ProbeResolveName.Call(req, GetCWD(), config).get<ProtocolResolveName::Rsp>();
    ASSERT_EQ(rsp.answers.size(), 1u);

    EXPECT_EQ(rsp.answers[0].code, 0);
    ASSERT_FALSE(rsp.answers[0].addresses.empty());
    EXPECT_EQ(rsp.answers[0].addresses[0], "127.0.0.1");
    EXPECT_NE(rsp.answers[0].addresses[0], "10.9.9.9");

    ASSERT_TRUE(tree.Verify());
}
