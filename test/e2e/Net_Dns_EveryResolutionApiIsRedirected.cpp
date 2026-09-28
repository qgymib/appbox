#include "probe/ResolveName.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/NetworkIsolationBuilder.hpp"
#include <cstddef>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Net;
using namespace appbox::test;

namespace
{

/** Hostname the isolation file redirects. */
constexpr const char* kRedirectedName = "appbox-spike.invalid";

/** Hostname the isolation file does not list. */
constexpr const char* kUnlistedName = "appbox-spike-miss.invalid";

/** Address the redirected hostname resolves to. */
constexpr const char* kRedirectAddress = "127.0.0.1";

/** The name resolution APIs a question is asked with, in the order of the case. */
const std::vector<ProtocolResolveName::Api> kApis = {
    ProtocolResolveName::Api::GetAddrInfo,   ProtocolResolveName::Api::GetAddrInfoAnsi,
    ProtocolResolveName::Api::GetAddrInfoEx, ProtocolResolveName::Api::GetHostByName,
    ProtocolResolveName::Api::DnsQuery,      ProtocolResolveName::Api::DnsQueryAnsi,
    ProtocolResolveName::Api::DnsQueryWide,
};

/** The ANSI interfaces of the two libraries, which the fallback is checked with. */
const std::vector<ProtocolResolveName::Api> kAnsiApis = {
    ProtocolResolveName::Api::GetAddrInfoAnsi,
    ProtocolResolveName::Api::DnsQueryAnsi,
};

} // namespace

/**
 * Condition:
 * 1. The DNS redirections of the network workspace list a hostname which the
 *    host cannot resolve, together with the address it resolves to.
 * 2. The sandboxed application resolves the hostname with every name
 *    resolution API the sandbox hooks: the wide, the ANSI and the extended
 *    entry point of winsock, its legacy entry point and the UTF-8, the ANSI and
 *    the wide entry point of the DNS client.
 *
 * Expected:
 * 1. Every call reports success and answers with the redirect address, so
 *    every hook of the name resolution redirects the name.
 * 2. The ANSI entry points keep the resolution of the host for a hostname the
 *    isolation file does not list: the host cannot resolve it either, so the
 *    call fails instead of being answered with an invented address.
 */
TEST_F(E2E_Net, Dns_EveryResolutionApiIsRedirected)
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

    ProtocolResolveName::Req req;
    for (const auto api : kApis)
    {
        req.questions.push_back({ kRedirectedName, api, AF_UNSPEC, DNS_TYPE_A });
    }
    for (const auto api : kAnsiApis)
    {
        req.questions.push_back({ kUnlistedName, api, AF_UNSPEC, DNS_TYPE_A });
    }

    const auto rsp = ProbeResolveName.Call(req, GetCWD(), config).get<ProtocolResolveName::Rsp>();
    ASSERT_EQ(rsp.answers.size(), req.questions.size());

    /* Every API of the name resolution answers the redirected hostname. */
    for (std::size_t index = 0; index < kApis.size(); ++index)
    {
        EXPECT_EQ(rsp.answers[index].code, 0) << "API " << index;
        ASSERT_EQ(rsp.answers[index].addresses.size(), 1u) << "API " << index;
        EXPECT_EQ(rsp.answers[index].addresses[0], kRedirectAddress) << "API " << index;
    }

    /* A hostname which is not listed keeps the resolution of the host. */
    for (std::size_t index = kApis.size(); index < req.questions.size(); ++index)
    {
        EXPECT_NE(rsp.answers[index].code, 0) << "API " << index;
        EXPECT_TRUE(rsp.answers[index].addresses.empty()) << "API " << index;
    }

    ASSERT_TRUE(tree.Verify());
}
