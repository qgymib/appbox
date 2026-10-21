#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include "send.hpp"

T_send sys_send = nullptr;

/**
 * @brief Detour of send().
 *
 * A datagram socket which is connected has no address in its call, so the
 * target is the one the connection recorded: the payload is wrapped by the
 * protocol of the proxy and sent to the relay of the association, which is what
 * a caller of `send` expects on a connected datagram socket. A stream socket
 * already reaches its target through the connection the proxy made, so its call
 * keeps the path of the host.
 */
static int WSAAPI Hook_send(SOCKET s, const char* buf, int len, int flags)
{
    appbox::network::Proxy* proxy = appbox::network::ProxyOfProcess();
    if (proxy == nullptr || buf == nullptr || len < 0 || !proxy->ProxiesDatagram(s))
    {
        return sys_send(s, buf, len, flags);
    }

    appbox::network::socks5::Endpoint target;
    if (!proxy->PeerOf(s, target))
    {
        /* A datagram socket which is not connected has no target to carry. */
        return sys_send(s, buf, len, flags);
    }

    WSABUF buffer;
    buffer.len = static_cast<ULONG>(len);
    buffer.buf = const_cast<char*>(buf);

    return proxy->SendDatagram(s, target, &buffer, 1);
}

static void LoadSend()
{
    sys_send = reinterpret_cast<T_send>(GetProcAddress(appbox::sys.h_ws2_32, "send"));
}

appbox::HookRecord appbox::HookSend = {
    "send",
    LoadSend,
    (void**)&sys_send,
    Hook_send,
};
