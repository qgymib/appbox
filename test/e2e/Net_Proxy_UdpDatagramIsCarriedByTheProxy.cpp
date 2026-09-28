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
 * 1. The proxy of the network workspace carries the UDP traffic and names a
 *    SOCKS5 server of the case.
 * 2. The sandboxed application sends a datagram to a server of the case and
 *    reads the answer.
 *
 * Expected:
 * 1. The server of the case received the datagram from the address of the
 *    relay of the proxy, so the datagram was carried by the relay and not by
 *    the application.
 * 2. The application read the answer and was told that it came from the
 *    address it sent the datagram to, so the relay is invisible to it.
 */
TEST_F(E2E_Net, Proxy_UdpDatagramIsCarriedByTheProxy)
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
    proxy_config.udp = true;
    proxy_config.server = L"127.0.0.1";
    proxy_config.port = std::to_wstring(proxy.Port());
    ASSERT_TRUE(WriteNetworkIsolationFile(GetCWD(), {}, proxy_config));

    ProtocolSocketTraffic::Step step;
    step.operation = ProtocolSocketTraffic::Operation::UdpEcho;
    step.address = "127.0.0.1";
    step.port = echo.UdpPort();
    step.payload = "appbox-datagram";

    ProtocolSocketTraffic::Req req;
    req.steps.push_back(step);

    const auto rsp = ProbeSocketTraffic.Call(req, GetCWD(), config).get<ProtocolSocketTraffic::Rsp>();
    ASSERT_EQ(rsp.results.size(), 1u);
    EXPECT_EQ(rsp.results[0].code, 0) << "error: " << rsp.results[0].error;
    EXPECT_EQ(rsp.results[0].sent, 15);
    EXPECT_EQ(rsp.results[0].received, "appbox-datagram");

    /* The answer reports the address the datagram was sent to. */
    EXPECT_EQ(rsp.results[0].source, "127.0.0.1:" + std::to_string(echo.UdpPort()));

    /* The server of the case saw the datagram come from the relay. */
    const std::uint16_t relay_port = proxy.UdpRelayPort();
    ASSERT_NE(relay_port, 0);
    EXPECT_EQ(echo.LastUdpPeer(), "127.0.0.1:" + std::to_string(relay_port));

    const auto requests = proxy.Requests();
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0].command, 3);

    proxy.Stop();
    echo.Stop();

    ASSERT_TRUE(tree.Verify());
}
