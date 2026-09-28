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
 * 1. The network workspace holds a proxy configuration whose two protocols are
 *    switched off, and a SOCKS5 server of the case is listening.
 * 2. The sandboxed application connects to a server of the case and sends it a
 *    datagram.
 *
 * Expected:
 * 1. Both reach the server of the case, so the traffic keeps the path of the
 *    host while the proxy carries nothing.
 * 2. The SOCKS5 server received no request at all, so a configuration which is
 *    switched off is never used.
 */
TEST_F(E2E_Net, Proxy_DisabledTrafficKeepsTheDirectPath)
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
    proxy_config.tcp = false;
    proxy_config.udp = false;
    proxy_config.server = L"127.0.0.1";
    proxy_config.port = std::to_wstring(proxy.Port());
    ASSERT_TRUE(WriteNetworkIsolationFile(GetCWD(), {}, proxy_config));

    ProtocolSocketTraffic::Step connection;
    connection.operation = ProtocolSocketTraffic::Operation::TcpEcho;
    connection.address = "127.0.0.1";
    connection.port = echo.TcpPort();
    connection.payload = "appbox-proxy";

    ProtocolSocketTraffic::Step datagram;
    datagram.operation = ProtocolSocketTraffic::Operation::UdpEcho;
    datagram.address = "127.0.0.1";
    datagram.port = echo.UdpPort();
    datagram.payload = "appbox-datagram";

    ProtocolSocketTraffic::Req req;
    req.steps.push_back(connection);
    req.steps.push_back(datagram);

    const auto rsp = ProbeSocketTraffic.Call(req, GetCWD(), config).get<ProtocolSocketTraffic::Rsp>();
    ASSERT_EQ(rsp.results.size(), 2u);
    EXPECT_EQ(rsp.results[0].code, 0) << "error: " << rsp.results[0].error;
    EXPECT_EQ(rsp.results[0].received, "appbox-proxy");
    EXPECT_EQ(rsp.results[1].code, 0) << "error: " << rsp.results[1].error;
    EXPECT_EQ(rsp.results[1].received, "appbox-datagram");

    /* The server of the proxy was never asked for anything. */
    EXPECT_TRUE(proxy.Requests().empty());
    EXPECT_FALSE(proxy.SawCredentials());
    EXPECT_FALSE(proxy.SawRefusal());

    /* The datagram reached the server of the case, and no relay was opened. */
    EXPECT_FALSE(echo.LastUdpPeer().empty());
    EXPECT_EQ(proxy.UdpRelayPort(), 0);

    proxy.Stop();
    echo.Stop();

    ASSERT_TRUE(tree.Verify());
}
