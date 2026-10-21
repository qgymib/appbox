#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include "recv.hpp"

T_recv sys_recv = nullptr;

/**
 * @brief Detour of recv().
 *
 * A datagram socket which is connected receives the payload of a datagram of
 * the relay, and the address of the sender is dropped because the socket is
 * connected to a single target. A stream socket receives the bytes of the
 * connection the proxy made, so its call keeps the path of the host.
 */
static int WSAAPI Hook_recv(SOCKET s, char* buf, int len, int flags)
{
    appbox::network::Proxy* proxy = appbox::network::ProxyOfProcess();
    if (proxy == nullptr || buf == nullptr || len <= 0 || !proxy->ProxiesDatagram(s))
    {
        return sys_recv(s, buf, len, flags);
    }

    WSABUF buffer;
    buffer.len = static_cast<ULONG>(len);
    buffer.buf = buf;

    appbox::network::socks5::Endpoint source;
    const DWORD                       deadline = GetTickCount() + 30000;
    return proxy->ReceiveDatagram(s, &buffer, 1, source, deadline);
}

static void LoadRecv()
{
    sys_recv = reinterpret_cast<T_recv>(GetProcAddress(appbox::sys.h_ws2_32, "recv"));
}

appbox::HookRecord appbox::HookRecv = {
    "recv",
    LoadRecv,
    (void**)&sys_recv,
    Hook_recv,
};
