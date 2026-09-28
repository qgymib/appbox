#include "probe/SocketTraffic.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EchoServer.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/NetworkIsolationBuilder.hpp"
#include "utils/Socks5Server.hpp"

typedef appbox::test::CommonFixture E2E_Net;
using namespace appbox::test;

/**
 * Condition:
 * 1. The proxy of the network workspace carries the TCP traffic and carries a
 *    user name and a password, and the SOCKS5 server of the case asks for them.
 * 2. The sandboxed application connects to a server of the case.
 *
 * Expected:
 * 1. The connection is established, so the credentials were sent and accepted.
 * 2. The server of the case saw the credential exchange of the protocol.
 */
TEST_F(E2E_Net, Proxy_CredentialsAreSent)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"network", {}) })
    });
    /* clang-format on */

    auto config = tree.Build();

    EchoServer   echo;
    Socks5Server proxy;
    ASSERT_TRUE(echo.Start());
    ASSERT_TRUE(proxy.Start("appbox-user", "appbox-secret"));

    NetworkIsolationProxy proxy_config;
    proxy_config.tcp = true;
    proxy_config.server = L"127.0.0.1";
    proxy_config.port = std::to_wstring(proxy.Port());
    proxy_config.username = L"appbox-user";
    proxy_config.password = L"appbox-secret";
    ASSERT_TRUE(WriteNetworkIsolationFile(GetCWD(), {}, proxy_config));

    ProtocolSocketTraffic::Step step;
    step.operation = ProtocolSocketTraffic::Operation::TcpEcho;
    step.address = "127.0.0.1";
    step.port = echo.TcpPort();
    step.payload = "appbox-proxy";

    ProtocolSocketTraffic::Req req;
    req.steps.push_back(step);

    const auto rsp = ProbeSocketTraffic.Call(req, GetCWD(), config).get<ProtocolSocketTraffic::Rsp>();
    ASSERT_EQ(rsp.results.size(), 1u);
    EXPECT_EQ(rsp.results[0].code, 0) << "error: " << rsp.results[0].error;
    EXPECT_EQ(rsp.results[0].received, "appbox-proxy");

    EXPECT_TRUE(proxy.SawCredentials());
    EXPECT_FALSE(proxy.SawRefusal());
    EXPECT_EQ(proxy.Requests().size(), 1u);

    proxy.Stop();
    echo.Stop();

    ASSERT_TRUE(tree.Verify());
}
