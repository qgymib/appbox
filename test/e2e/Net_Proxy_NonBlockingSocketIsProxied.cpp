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
 * 1. The proxy of the network workspace carries the TCP traffic.
 * 2. The sandboxed application switches its socket to the non-blocking mode
 *    and connects to a server of the case.
 *
 * Expected:
 * 1. The connection succeeds, so the handshake of the protocol completes for a
 *    socket which does not block.
 * 2. The socket is still in the non-blocking mode after the call, so the
 *    sandbox never changes the mode the application asked for.
 */
TEST_F(E2E_Net, Proxy_NonBlockingSocketIsProxied)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {})
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
    ASSERT_TRUE(WriteNetworkIsolationFile(config, {}, proxy_config));

    ProtocolSocketTraffic::Step step;
    step.operation = ProtocolSocketTraffic::Operation::TcpConnectNonBlocking;
    step.address = "127.0.0.1";
    step.port = echo.TcpPort();
    step.timeout_ms = 500;

    ProtocolSocketTraffic::Req req;
    req.steps.push_back(step);

    const auto rsp = ProbeSocketTraffic.Call(req, GetCWD(), config).get<ProtocolSocketTraffic::Rsp>();
    ASSERT_EQ(rsp.results.size(), 1u);
    EXPECT_EQ(rsp.results[0].code, 0) << "error: " << rsp.results[0].error;
    EXPECT_TRUE(rsp.results[0].nonblocking);

    EXPECT_EQ(proxy.Requests().size(), 1u);

    proxy.Stop();
    echo.Stop();

    ASSERT_TRUE(tree.Verify());
}
