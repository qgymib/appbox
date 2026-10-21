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
 * 1. The proxy of the network workspace carries the TCP traffic and names a
 *    SOCKS5 server of the case.
 * 2. The sandboxed application connects with the extension function of the
 *    socket API instead of `connect`, which is the call a program uses when it
 *    resolves the function with `WSAIoctl`.
 *
 * Expected:
 * 1. The connection reaches the server of the case and the payload comes back,
 *    so a connection which is made through the extension function is carried
 *    as well.
 * 2. The SOCKS5 server received a CONNECT request which names the address and
 *    the port of the case.
 */
TEST_F(E2E_Net, Proxy_ConnectExIsCarriedByTheProxy)
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
    ASSERT_TRUE(proxy.Start("", ""));

    NetworkIsolationProxy proxy_config;
    proxy_config.tcp = true;
    proxy_config.server = L"127.0.0.1";
    proxy_config.port = std::to_wstring(proxy.Port());
    ASSERT_TRUE(WriteNetworkIsolationFile(GetCWD(), {}, proxy_config));

    ProtocolSocketTraffic::Step step;
    step.operation = ProtocolSocketTraffic::Operation::TcpConnectEx;
    step.address = "127.0.0.1";
    step.port = echo.TcpPort();
    step.payload = "appbox-connect-ex";

    ProtocolSocketTraffic::Req req;
    req.steps.push_back(step);

    const auto rsp = ProbeSocketTraffic.Call(req, GetCWD(), config).get<ProtocolSocketTraffic::Rsp>();
    ASSERT_EQ(rsp.results.size(), 1u);
    EXPECT_EQ(rsp.results[0].code, 0) << "error: " << rsp.results[0].error;
    EXPECT_EQ(rsp.results[0].received, "appbox-connect-ex");

    const auto requests = proxy.Requests();
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0].command, 1);
    EXPECT_EQ(requests[0].address, "127.0.0.1");
    EXPECT_EQ(requests[0].port, echo.TcpPort());

    proxy.Stop();
    echo.Stop();

    ASSERT_TRUE(tree.Verify());
}
