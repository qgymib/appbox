#include "probe/ResolveName.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/NetworkIsolationBuilder.hpp"

typedef appbox::test::CommonFixture E2E_Net;
using namespace appbox::test;

/**
 * Condition:
 * 1. The DNS redirections list a hostname with an IPv4 address and one with an
 *    IPv6 address, both of which the host cannot resolve.
 * 2. The sandboxed application asks for both families.
 *
 * Expected:
 * 1. A question is answered from the entry whose address fits the family of the
 *    question.
 * 2. A question of the other family keeps the resolution of the host, which
 *    fails for a name which only the isolation file knows, instead of being
 *    answered with an address of the wrong family.
 */
TEST_F(E2E_Net, Dns_FamilyOfTheRedirectIsHonoured)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {})
    });
    /* clang-format on */

    auto config = tree.Build();
    ASSERT_TRUE(WriteNetworkIsolationFile(
        config, {
                    { L"v4.appbox-spike.invalid", L"127.0.0.1" },
                    { L"v6.appbox-spike.invalid", L"::1"       }
    }));

    ProtocolResolveName::Req req;
    /* clang-format off */
    req.questions = {
        { "v4.appbox-spike.invalid", ProtocolResolveName::Api::GetAddrInfo, AF_INET,  DNS_TYPE_A },
        { "v6.appbox-spike.invalid", ProtocolResolveName::Api::GetAddrInfo, AF_INET6, DNS_TYPE_A },
        { "v4.appbox-spike.invalid", ProtocolResolveName::Api::GetAddrInfo, AF_INET6, DNS_TYPE_A },
        { "v6.appbox-spike.invalid", ProtocolResolveName::Api::GetAddrInfo, AF_INET,  DNS_TYPE_A },
    };
    /* clang-format on */

    const auto rsp = ProbeResolveName.Call(req, GetCWD(), config).get<ProtocolResolveName::Rsp>();
    ASSERT_EQ(rsp.answers.size(), 4u);

    /* The family of the question fits the address of the entry. */
    EXPECT_EQ(rsp.answers[0].code, 0);
    ASSERT_EQ(rsp.answers[0].addresses.size(), 1u);
    EXPECT_EQ(rsp.answers[0].addresses[0], "127.0.0.1");

    EXPECT_EQ(rsp.answers[1].code, 0);
    ASSERT_EQ(rsp.answers[1].addresses.size(), 1u);
    EXPECT_EQ(rsp.answers[1].addresses[0], "::1");

    /* The family of the question does not fit, so the name is not answered. */
    EXPECT_NE(rsp.answers[2].code, 0);
    EXPECT_TRUE(rsp.answers[2].addresses.empty());

    EXPECT_NE(rsp.answers[3].code, 0);
    EXPECT_TRUE(rsp.answers[3].addresses.empty());

    ASSERT_TRUE(tree.Verify());
}
