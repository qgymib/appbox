#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include "sendto.hpp"

T_sendto sys_sendto = nullptr;

/**
 * @brief Detour of sendto().
 *
 * A datagram of a datagram socket is wrapped by the protocol of the proxy and
 * sent to the relay of the association of the socket, so the payload reaches
 * the target through the server. The caller is answered with the number of the
 * bytes it handed over, which is what a caller of sendto() expects. A socket of
 * another kind, and every socket of a process which has no proxy, keeps the
 * path of the host.
 */
static int WSAAPI Hook_sendto(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int tolen)
{
    appbox::network::Proxy* proxy = appbox::network::ProxyOfProcess();
    if (proxy == nullptr || buf == nullptr || to == nullptr || len < 0 || !proxy->ProxiesDatagram(s))
    {
        return sys_sendto(s, buf, len, flags, to, tolen);
    }

    appbox::network::socks5::Endpoint target;
    if (!appbox::network::EndpointFromSockaddr(to, tolen, target))
    {
        LOG_W("the address of a datagram cannot be carried by the proxy");
        return sys_sendto(s, buf, len, flags, to, tolen);
    }

    WSABUF buffer;
    buffer.len = static_cast<ULONG>(len);
    buffer.buf = const_cast<char*>(buf);

    return proxy->SendDatagram(s, target, &buffer, 1);
}

static void LoadSendTo()
{
    sys_sendto = reinterpret_cast<T_sendto>(GetProcAddress(appbox::sys.h_ws2_32, "sendto"));
}

appbox::HookRecord appbox::HookSendTo = {
    "sendto",
    LoadSendTo,
    (void**)&sys_sendto,
    Hook_sendto,
};
