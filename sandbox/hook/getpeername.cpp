#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include "getpeername.hpp"

T_getpeername sys_getpeername = nullptr;

/**
 * @brief Detour of getpeername().
 *
 * A socket which the proxy connected is connected to the server of the proxy,
 * so the stack would report the address of the server. The call answers the
 * address the application asked for instead, which is what a program reads to
 * learn its peer, so the proxy stays invisible to it. A socket the proxy did
 * not connect keeps the answer of the host.
 */
static int WSAAPI Hook_getpeername(SOCKET s, sockaddr* name, int* namelen)
{
    appbox::network::Proxy*           proxy = appbox::network::ProxyOfProcess();
    appbox::network::socks5::Endpoint peer;
    if (proxy == nullptr || name == nullptr || namelen == nullptr || !proxy->PeerOf(s, peer))
    {
        return sys_getpeername(s, name, namelen);
    }

    if (!appbox::network::WriteAddress(peer, AF_UNSPEC, name, namelen))
    {
        LOG_W("the peer of a socket cannot be reported to the application");
        WSASetLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }
    return 0;
}

static void LoadGetPeerName()
{
    sys_getpeername = reinterpret_cast<T_getpeername>(GetProcAddress(appbox::sys.h_ws2_32, "getpeername"));
}

appbox::HookRecord appbox::HookGetPeerName = {
    "getpeername",
    LoadGetPeerName,
    (void**)&sys_getpeername,
    Hook_getpeername,
};
