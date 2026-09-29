#include "probe/SocketTraffic.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EchoServer.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/NetworkIsolationBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "utils/Socks5Server.hpp"
#include "SandboxLayout.hpp"
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Patch;
using namespace appbox::test;

/**
 * Condition:
 * 1. A SOCKS5 server of the case is listening for every layer of the case.
 * 2. `00-foo.zip` configures the first server as the proxy of its layer and
 *    `01-bar.zip` configures the second one, so the two layers name a proxy of
 *    their own.
 * 3. The sandboxed application connects to a server of the case and sends it a
 *    payload.
 *
 * Expected:
 * 1. The connection is carried by the server of `01-bar.zip`, so the proxy of
 *    a package replaces the proxy of the layers below it.
 * 2. The server of `00-foo.zip` received no request at all, so the proxy below
 *    the last package is not used in parallel.
 */
TEST_F(E2E_Patch, NetworkProxyOfTheLastLayerWins)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"network", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    EchoServer   echo;
    Socks5Server first;
    Socks5Server second;
    ASSERT_TRUE(echo.Start());
    ASSERT_TRUE(first.Start("", ""));
    ASSERT_TRUE(second.Start("", ""));

    NetworkIsolationProxy proxy_of_the_first;
    proxy_of_the_first.tcp = true;
    proxy_of_the_first.server = L"127.0.0.1";
    proxy_of_the_first.port = std::to_wstring(first.Port());

    NetworkIsolationProxy proxy_of_the_second;
    proxy_of_the_second.tcp = true;
    proxy_of_the_second.server = L"127.0.0.1";
    proxy_of_the_second.port = std::to_wstring(second.Port());

    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));
    ASSERT_TRUE(WritePatchPackage(patch_dir / L"00-foo.zip", {}, {}, {}, PatchNetwork{ {}, proxy_of_the_first }));
    ASSERT_TRUE(WritePatchPackage(patch_dir / L"01-bar.zip", {}, {}, {}, PatchNetwork{ {}, proxy_of_the_second }));

    ProtocolSocketTraffic::Step connection;
    connection.operation = ProtocolSocketTraffic::Operation::TcpEcho;
    connection.address = "127.0.0.1";
    connection.port = echo.TcpPort();
    connection.payload = "appbox-patch-proxy";

    ProtocolSocketTraffic::Req req;
    req.steps.push_back(connection);

    const auto rsp = ProbeSocketTraffic.Call(req, GetCWD(), config).get<ProtocolSocketTraffic::Rsp>();
    ASSERT_EQ(rsp.results.size(), 1u);
    EXPECT_EQ(rsp.results[0].code, 0) << "error: " << rsp.results[0].error;
    EXPECT_EQ(rsp.results[0].received, "appbox-patch-proxy");

    /* The proxy of the last package carried the connection. */
    EXPECT_FALSE(second.Requests().empty());
    EXPECT_EQ(second.Requests()[0].address, "127.0.0.1");
    EXPECT_EQ(second.Requests()[0].port, echo.TcpPort());

    /* The proxy of the package below it was replaced and not used. */
    EXPECT_TRUE(first.Requests().empty());

    first.Stop();
    second.Stop();
    echo.Stop();

    ASSERT_TRUE(tree.Verify());
}
