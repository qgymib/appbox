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

namespace
{

/**
 * @brief The document of a layer whose network workspace holds nothing.
 *
 * The packer always writes the network isolation file of a patch package, so a
 * package whose workspace lists no redirection and configures no proxy carries
 * a document with an empty entry list and no `proxy` member. The text is
 * spelled out here, because the builder of the helper writes no file at all
 * while the case describes nothing.
 */
constexpr const char* kEmptyNetworkDocument = R"({"version":1,"entries":[]})";

} // namespace

/**
 * Condition:
 * 1. The resources of the archive configure a SOCKS5 server of the case as
 *    their proxy.
 * 2. `00-foo.zip` carries the network document of a workspace which lists no
 *    redirection and configures no proxy, so the layer names no proxy of its
 *    own.
 * 3. The sandboxed application connects to a server of the case and sends it a
 *    payload.
 *
 * Expected:
 * 1. The connection is carried by the proxy of the archive, so a layer which
 *    names no proxy keeps the proxy of the layers below it.
 */
TEST_F(E2E_Patch, NetworkProxyBelowIsKept)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"network", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    EchoServer   echo;
    Socks5Server proxy;
    ASSERT_TRUE(echo.Start());
    ASSERT_TRUE(proxy.Start("", ""));

    NetworkIsolationProxy proxy_config;
    proxy_config.tcp = true;
    proxy_config.server = L"127.0.0.1";
    proxy_config.port = std::to_wstring(proxy.Port());
    ASSERT_TRUE(WriteNetworkIsolationFile(GetCWD(), {}, proxy_config));

    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));
    ASSERT_TRUE(
        WritePatchPackage(patch_dir / L"00-foo.zip", {}, {}, {}, PatchNetwork{ {}, {}, kEmptyNetworkDocument }));

    ProtocolSocketTraffic::Step connection;
    connection.operation = ProtocolSocketTraffic::Operation::TcpEcho;
    connection.address = "127.0.0.1";
    connection.port = echo.TcpPort();
    connection.payload = "appbox-patch-kept";

    ProtocolSocketTraffic::Req req;
    req.steps.push_back(connection);

    const auto rsp = ProbeSocketTraffic.Call(req, GetCWD(), config).get<ProtocolSocketTraffic::Rsp>();
    ASSERT_EQ(rsp.results.size(), 1u);
    EXPECT_EQ(rsp.results[0].code, 0) << "error: " << rsp.results[0].error;
    EXPECT_EQ(rsp.results[0].received, "appbox-patch-kept");

    /* The proxy of the archive carried the connection. */
    ASSERT_FALSE(proxy.Requests().empty());
    EXPECT_EQ(proxy.Requests()[0].address, "127.0.0.1");
    EXPECT_EQ(proxy.Requests()[0].port, echo.TcpPort());

    proxy.Stop();
    echo.Stop();

    ASSERT_TRUE(tree.Verify());
}
