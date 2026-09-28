#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include "connect.hpp"

T_connect sys_connect = nullptr;

/**
 * @brief Detour of connect().
 *
 * A stream socket is connected to the target through the proxy of the network
 * isolation: the connection to the server and the handshake of the protocol
 * are performed before the call returns, so the socket is connected to the
 * target when the application gets the control back and the failure of the
 * proxy is the failure of the connection. A socket of another kind, and every
 * socket of a process which has no proxy, keeps the connection of the host.
 */
static int WSAAPI Hook_connect(SOCKET s, const sockaddr* name, int namelen)
{
    appbox::network::Proxy* proxy = appbox::network::ProxyOfProcess();
    if (proxy == nullptr || !proxy->ProxiesTcp())
    {
        return sys_connect(s, name, namelen);
    }

    if (proxy->KindOf(s) != appbox::network::SocketKind::Stream)
    {
        /*
         * A connected datagram socket sends with send() instead of sendto(),
         * which is not a call the sandbox carries.
         */
        LOG_W("the connection of a datagram socket keeps the direct path");
        return sys_connect(s, name, namelen);
    }

    appbox::network::socks5::Endpoint target;
    if (!appbox::network::EndpointFromSockaddr(name, namelen, target))
    {
        LOG_W("the address of a connection cannot be carried by the proxy");
        return sys_connect(s, name, namelen);
    }

    return proxy->ConnectTcp(s, target);
}

static void LoadConnect()
{
    sys_connect = reinterpret_cast<T_connect>(GetProcAddress(appbox::sys.h_ws2_32, "connect"));
}

appbox::HookRecord appbox::HookConnect = {
    "connect",
    LoadConnect,
    (void**)&sys_connect,
    Hook_connect,
};
