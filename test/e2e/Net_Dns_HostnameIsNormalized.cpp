#include "probe/ResolveName.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/NetworkIsolationBuilder.hpp"
#include <cstddef>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Net;
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
TEST_F(E2E_Net, Dns_HostnameIsNormalized)
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

    const std::vector<std::string> names = { "update.example.com", "UPDATE.EXAMPLE.COM.", "Update.Example.Com" };

    ProtocolResolveName::Req req;
    for (const auto& name : names)
    {
        req.questions.push_back({ name, ProtocolResolveName::Api::GetAddrInfo, AF_UNSPEC, DNS_TYPE_A });
    }

    const auto rsp = ProbeResolveName.Call(req, GetCWD(), config).get<ProtocolResolveName::Rsp>();
    ASSERT_EQ(rsp.answers.size(), names.size());

    for (std::size_t index = 0; index < names.size(); ++index)
    {
        EXPECT_EQ(rsp.answers[index].code, 0) << names[index];
        ASSERT_EQ(rsp.answers[index].addresses.size(), 1u) << names[index];
        EXPECT_EQ(rsp.answers[index].addresses[0], "127.0.0.1") << names[index];
    }

    ASSERT_TRUE(tree.Verify());
}
