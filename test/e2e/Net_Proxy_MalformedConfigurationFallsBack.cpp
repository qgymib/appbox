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
 * 1. The network isolation file carries a proxy whose port cannot be used, and
 *    a SOCKS5 server of the case is listening on another port.
 * 2. The sandboxed application connects to a server of the case.
 *
 * Expected:
 * 1. The application reaches the server of the case, so a proxy the sandbox
 *    cannot use is read as a session without a proxy and the call keeps the
 *    path of the host.
 * 2. The SOCKS5 server received no request, and the application was not
 *    blocked from starting.
 */
TEST_F(E2E_Net, Proxy_MalformedConfigurationFallsBack)
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

    /*
     * The document names the server of the case, but its port carries a
     * leading zero, which is a port the packer and the sandbox both refuse.
     */
    const std::string text = std::string("{\n") + "  \"version\": 1,\n" + "  \"entries\": [],\n" +
                             "  \"proxy\": { \"type\": \"socks5\", \"tcp\": true, \"udp\": true,\n" +
                             "               \"server\": \"127.0.0.1\", \"port\": \"0" + std::to_string(proxy.Port()) +
                             "\",\n" + "               \"username\": \"\", \"password\": \"\" }\n" + "}\n";
    ASSERT_TRUE(WriteNetworkIsolationFileText(GetCWD(), text));

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

    EXPECT_TRUE(proxy.Requests().empty());

    proxy.Stop();
    echo.Stop();

    ASSERT_TRUE(tree.Verify());
}
