#include "probe/ResolveName.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/NetworkIsolationBuilder.hpp"

typedef appbox::test::CommonFixture Net;
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
TEST_F(Net, Dns_FamilyOfTheRedirectIsHonoured)
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

    const auto resolve = [&config, this](const char* name, int family) {
        ProtocolResolveName::Req req;
        req.name = name;
        req.api = ProtocolResolveName::Req::Api::GetAddrInfo;
        req.family = family;
        return ProbeResolveName.Call(req, GetCWD(), config).get<ProtocolResolveName::Rsp>();
    };

    /* The family of the question fits the address of the entry. */
    {
        const auto rsp = resolve("v4.appbox-spike.invalid", AF_INET);
        EXPECT_EQ(rsp.code, 0);
        ASSERT_EQ(rsp.addresses.size(), 1u);
        EXPECT_EQ(rsp.addresses[0], "127.0.0.1");
    }
    {
        const auto rsp = resolve("v6.appbox-spike.invalid", AF_INET6);
        EXPECT_EQ(rsp.code, 0);
        ASSERT_EQ(rsp.addresses.size(), 1u);
        EXPECT_EQ(rsp.addresses[0], "::1");
    }

    /* The family of the question does not fit, so the name is not answered. */
    {
        const auto rsp = resolve("v4.appbox-spike.invalid", AF_INET6);
        EXPECT_NE(rsp.code, 0);
        EXPECT_TRUE(rsp.addresses.empty());
    }
    {
        const auto rsp = resolve("v6.appbox-spike.invalid", AF_INET);
        EXPECT_NE(rsp.code, 0);
        EXPECT_TRUE(rsp.addresses.empty());
    }

    ASSERT_TRUE(tree.Verify());
}
