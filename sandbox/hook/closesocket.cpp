#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include "closesocket.hpp"

T_closesocket sys_closesocket = nullptr;

/**
 * @brief Detour of closesocket().
 *
 * The state of the socket is released with the socket, so the control
 * connection of an association is closed as well and a closed socket never
 * leaves a connection to the server of the proxy behind.
 */
static int WSAAPI Hook_closesocket(SOCKET s)
{
    appbox::network::Proxy* proxy = appbox::network::ProxyOfProcess();
    if (proxy != nullptr)
    {
        proxy->Close(s);
    }

    return sys_closesocket(s);
}

static void LoadCloseSocket()
{
    sys_closesocket = reinterpret_cast<T_closesocket>(GetProcAddress(appbox::sys.h_ws2_32, "closesocket"));
}

appbox::HookRecord appbox::HookCloseSocket = {
    "closesocket",
    LoadCloseSocket,
    (void**)&sys_closesocket,
    Hook_closesocket,
};
