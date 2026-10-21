#include "utils/Winsock.hpp" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include "network/Extensions.hpp"
#include "WSAIoctl.hpp"
#include <cstring>

T_WSAIoctl sys_WSAIoctl = nullptr;

namespace
{

/** Identifier of the extension function which connects a socket. */
constexpr GUID kWsaIdConnectEx = {
    0x25a207b9, 0xddf3, 0x4660, { 0x8e, 0xe9, 0x76, 0xe5, 0x8c, 0x74, 0x06, 0x3e }
};

/** Identifier of the extension function which sends a message. */
constexpr GUID kWsaIdWSASendMsg = {
    0xa441e712, 0x754f, 0x43ca, { 0x84, 0xa7, 0x0d, 0xee, 0x44, 0xcf, 0x60, 0x6d }
};

/** Identifier of the extension function which receives a message. */
constexpr GUID kWsaIdWSARecvMsg = {
    0xf689d7c8, 0x6f1f, 0x436b, { 0x8a, 0x53, 0xe5, 0x4f, 0xe3, 0x51, 0xc3, 0x22 }
};

/**
 * @brief Whether two identifiers name the same extension function.
 * @param[in] left Identifier of a caller.
 * @param[in] right Identifier the sandbox knows.
 * @return true when the identifiers are the same.
 */
bool SameIdentifier(const GUID& left, const GUID& right)
{
    return std::memcmp(&left, &right, sizeof(GUID)) == 0;
}

/**
 * @brief Replace the address of an extension function the system answered.
 *
 * The functions which have no import library are resolved by their identifier,
 * so the answer of the call is the place where the sandbox takes them over: a
 * program which asks for `ConnectEx`, `WSASendMsg` or `WSARecvMsg` is answered
 * with the function of the sandbox while the proxy carries the traffic.
 *
 * @param[in] code Control code of the call.
 * @param[in] in_buffer Buffer of the call.
 * @param[in] in_size Size of the buffer of the call.
 * @param[in] out_buffer Buffer of the answer.
 * @param[in] out_size Size of the buffer of the answer.
 */
void ReplaceExtension(DWORD code, LPVOID in_buffer, DWORD in_size, LPVOID out_buffer, DWORD out_size)
{
    if (code != SIO_GET_EXTENSION_FUNCTION_POINTER || in_buffer == nullptr || out_buffer == nullptr ||
        in_size < sizeof(GUID) || out_size < sizeof(void*))
    {
        return;
    }

    appbox::network::Proxy* proxy = appbox::network::ProxyOfProcess();
    if (proxy == nullptr || (!proxy->ProxiesTcp() && !proxy->ProxiesUdp()))
    {
        return;
    }

    GUID identifier;
    std::memcpy(&identifier, in_buffer, sizeof(identifier));

    if (SameIdentifier(identifier, kWsaIdConnectEx))
    {
        *reinterpret_cast<void**>(out_buffer) = reinterpret_cast<void*>(&appbox::network::SandboxConnectEx);
    }
    else if (SameIdentifier(identifier, kWsaIdWSASendMsg))
    {
        *reinterpret_cast<void**>(out_buffer) = reinterpret_cast<void*>(&appbox::network::SandboxWSASendMsg);
    }
    else if (SameIdentifier(identifier, kWsaIdWSARecvMsg))
    {
        *reinterpret_cast<void**>(out_buffer) = reinterpret_cast<void*>(&appbox::network::SandboxWSARecvMsg);
    }
}

} // namespace

/**
 * @brief Detour of WSAIoctl().
 *
 * The call reaches the system first, and the address it answered is replaced by
 * the function of the sandbox for the extensions the proxy carries. Every other
 * control code keeps the answer of the host.
 */
static int WSAAPI Hook_WSAIoctl(SOCKET s, DWORD dwIoControlCode, LPVOID lpvInBuffer, DWORD cbInBuffer,
                                LPVOID lpvOutBuffer, DWORD cbOutBuffer, LPDWORD lpcbBytesReturned,
                                LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    const int result = sys_WSAIoctl(s, dwIoControlCode, lpvInBuffer, cbInBuffer, lpvOutBuffer, cbOutBuffer,
                                    lpcbBytesReturned, lpOverlapped, lpCompletionRoutine);
    if (result == 0)
    {
        ReplaceExtension(dwIoControlCode, lpvInBuffer, cbInBuffer, lpvOutBuffer, cbOutBuffer);
    }
    return result;
}

static void LoadWSAIoctl()
{
    sys_WSAIoctl = reinterpret_cast<T_WSAIoctl>(GetProcAddress(appbox::sys.h_ws2_32, "WSAIoctl"));
}

appbox::HookRecord appbox::HookWSAIoctl = {
    "WSAIoctl",
    LoadWSAIoctl,
    (void**)&sys_WSAIoctl,
    Hook_WSAIoctl,
};
