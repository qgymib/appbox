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
 *    port of the loopback address where nothing listens.
 * 2. The sandboxed application connects to a server of the case which is
 *    listening.
 *
 * Expected:
 * 1. The connection of the application fails with the error of the connection
 *    to the server of the proxy, so a proxy which cannot be reached fails the
 *    call instead of carrying the traffic directly.
 * 2. The server of the case never accepted a connection.
 */
TEST_F(E2E_Net, Proxy_UnreachableServerFailsTheConnect)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"network", {}) })
    });
    /* clang-format on */

    auto config = tree.Build();

    EchoServer echo;
    ASSERT_TRUE(echo.Start());

    /*
     * A port of the loopback address is reserved and released again, so
     * nothing listens on it while the case runs.
     */
    std::uint16_t free_port = 0;
    {
        Socks5Server reserved;
        ASSERT_TRUE(reserved.Start("", ""));
        free_port = reserved.Port();
        reserved.Stop();
    }
    ASSERT_NE(free_port, 0);

    NetworkIsolationProxy proxy_config;
    proxy_config.tcp = true;
    proxy_config.server = L"127.0.0.1";
    proxy_config.port = std::to_wstring(free_port);
    ASSERT_TRUE(WriteNetworkIsolationFile(GetCWD(), {}, proxy_config));

    ProtocolSocketTraffic::Step step;
    step.operation = ProtocolSocketTraffic::Operation::TcpConnect;
    step.address = "127.0.0.1";
    step.port = echo.TcpPort();

    ProtocolSocketTraffic::Req req;
    req.steps.push_back(step);

    const auto rsp = ProbeSocketTraffic.Call(req, GetCWD(), config).get<ProtocolSocketTraffic::Rsp>();
    ASSERT_EQ(rsp.results.size(), 1u);
    EXPECT_EQ(rsp.results[0].code, -1);
    EXPECT_NE(rsp.results[0].error, 0);

    /* The application was never carried to the target directly. */
    EXPECT_FALSE(echo.SawTcpConnection());

    echo.Stop();

    ASSERT_TRUE(tree.Verify());
}
