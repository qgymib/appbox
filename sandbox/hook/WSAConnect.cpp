#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include "WSAConnect.hpp"

T_WSAConnect sys_WSAConnect = nullptr;

/**
 * @brief Detour of WSAConnect().
 *
 * The call is the extended form of connect(): the stream socket is connected
 * to the target through the proxy of the network isolation, and a socket of
 * another kind keeps the connection of the host.
 *
 * The caller data and the quality of service of the call describe a connection
 * which is established by the proxy, so they are not sent: a request of the
 * protocol carries the target and nothing else.
 */
static int WSAAPI Hook_WSAConnect(SOCKET s, const sockaddr* name, int namelen, LPWSABUF lpCallerData,
                                  LPWSABUF lpCalleeData, LPQOS lpSQOS, LPQOS lpGQOS)
{
    appbox::network::Proxy* proxy = appbox::network::ProxyOfProcess();
    if (proxy == nullptr || !proxy->ProxiesTcp())
    {
        return sys_WSAConnect(s, name, namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS);
    }

    if (proxy->KindOf(s) != appbox::network::SocketKind::Stream)
    {
        LOG_W("the connection of a datagram socket keeps the direct path");
        return sys_WSAConnect(s, name, namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS);
    }

    appbox::network::socks5::Endpoint target;
    if (!appbox::network::EndpointFromSockaddr(name, namelen, target))
    {
        LOG_W("the address of a connection cannot be carried by the proxy");
        return sys_WSAConnect(s, name, namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS);
    }

    if (lpCallerData != nullptr)
    {
        LOG_W("the caller data of a connection is not carried by the proxy");
    }

    return proxy->ConnectTcp(s, target);
}

static void LoadWSAConnect()
{
    sys_WSAConnect = reinterpret_cast<T_WSAConnect>(GetProcAddress(appbox::sys.h_ws2_32, "WSAConnect"));
}

appbox::HookRecord appbox::HookWSAConnect = {
    "WSAConnect",
    LoadWSAConnect,
    (void**)&sys_WSAConnect,
    Hook_WSAConnect,
};
