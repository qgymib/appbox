#include "utils/Winsock.hpp" /* Must be first include file */
#include "Extensions.hpp"
#include "Completion.hpp"
#include "Proxy.hpp"
#include "hook/WSAIoctl.hpp"
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include <mutex>

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

/** Signature of the extension function which connects a socket. */
using ConnectExFn = BOOL(PASCAL*)(SOCKET, const sockaddr*, int, PVOID, DWORD, LPDWORD, LPOVERLAPPED);

/** Signature of the extension function which sends a message. */
using SendMsgFn = INT(WSAAPI*)(SOCKET, LPWSAMSG, DWORD, LPDWORD, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);

/** Signature of the extension function which receives a message. */
using RecvMsgFn = INT(WSAAPI*)(SOCKET, LPWSAMSG, LPDWORD, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);

/**
 * @brief Read the address of an extension function of the system.
 *
 * The question is put through the entry point the hooks saved, so it reaches
 * the system and not the answer of the sandbox, and it is put on a socket of
 * its own so the state of a socket of the application is not touched.
 *
 * @param[in] identifier Identifier of the extension function.
 * @return The address of the function, null when it could not be resolved.
 */
void* OriginalExtension(const GUID& identifier)
{
    if (sys_WSAIoctl == nullptr)
    {
        return nullptr;
    }

    const SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET)
    {
        return nullptr;
    }

    void*     address = nullptr;
    DWORD     returned = 0;
    const int result = sys_WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, const_cast<GUID*>(&identifier),
                                    sizeof(identifier), &address, sizeof(address), &returned, nullptr, nullptr);
    ::closesocket(socket);

    return result == 0 ? address : nullptr;
}

/**
 * @brief The function of the system which connects a socket.
 * @return The address of the function, null when it could not be resolved.
 */
ConnectExFn OriginalConnectEx()
{
    static ConnectExFn address = reinterpret_cast<ConnectExFn>(OriginalExtension(kWsaIdConnectEx));
    return address;
}

/**
 * @brief The function of the system which sends a message.
 * @return The address of the function, null when it could not be resolved.
 */
SendMsgFn OriginalWSASendMsg()
{
    static SendMsgFn address = reinterpret_cast<SendMsgFn>(OriginalExtension(kWsaIdWSASendMsg));
    return address;
}

/**
 * @brief The function of the system which receives a message.
 * @return The address of the function, null when it could not be resolved.
 */
RecvMsgFn OriginalWSARecvMsg()
{
    static RecvMsgFn address = reinterpret_cast<RecvMsgFn>(OriginalExtension(kWsaIdWSARecvMsg));
    return address;
}

/**
 * @brief The family of the address a message reports.
 * @param[in] message Message of the caller.
 * @return The family, `AF_UNSPEC` when the message names no address.
 */
int FamilyOf(const WSAMSG* message)
{
    if (message == nullptr || message->name == nullptr)
    {
        return AF_UNSPEC;
    }
    return message->name->sa_family;
}

} // namespace

BOOL PASCAL appbox::network::SandboxConnectEx(SOCKET socket, const sockaddr* name, int namelen, PVOID send_buffer,
                                              DWORD send_length, LPDWORD bytes_sent, LPOVERLAPPED overlapped)
{
    appbox::network::Proxy*           proxy = appbox::network::ProxyOfProcess();
    appbox::network::socks5::Endpoint target;
    if (proxy == nullptr || !proxy->ProxiesTcp() || name == nullptr || overlapped == nullptr ||
        !appbox::network::EndpointFromSockaddr(name, namelen, target))
    {
        const ConnectExFn original = OriginalConnectEx();
        if (original == nullptr)
        {
            WSASetLastError(WSAEOPNOTSUPP);
            return FALSE;
        }
        return original(socket, name, namelen, send_buffer, send_length, bytes_sent, overlapped);
    }

    if (proxy->ConnectTcp(socket, target) == SOCKET_ERROR)
    {
        return FALSE;
    }

    DWORD sent = 0;
    if (send_buffer != nullptr && send_length != 0)
    {
        int written = 0;
        if (!proxy->SendStream(socket, static_cast<const char*>(send_buffer), static_cast<int>(send_length), written))
        {
            return FALSE;
        }
        sent = static_cast<DWORD>(written);
    }
    if (bytes_sent != nullptr)
    {
        *bytes_sent = sent;
    }

    /*
     * The connection is made, so the operation completes: a caller of this
     * function always waits for a completion, and reporting it here keeps the
     * shape of the call the same whether the proxy or the system answered.
     */
    appbox::network::CompleteOverlapped(socket, overlapped, sent, 0);
    WSASetLastError(WSA_IO_PENDING);
    return FALSE;
}

INT WSAAPI appbox::network::SandboxWSASendMsg(SOCKET socket, LPWSAMSG message, DWORD flags, LPDWORD sent,
                                              LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    appbox::network::Proxy*           proxy = appbox::network::ProxyOfProcess();
    appbox::network::socks5::Endpoint target;
    const bool                        carried =
        proxy != nullptr && message != nullptr && proxy->ProxiesDatagram(socket) &&
        ((message->name != nullptr && appbox::network::EndpointFromSockaddr(message->name, message->namelen, target)) ||
         (message->name == nullptr && proxy->PeerOf(socket, target)));
    if (!carried)
    {
        const SendMsgFn original = OriginalWSASendMsg();
        if (original == nullptr)
        {
            WSASetLastError(WSAEOPNOTSUPP);
            return SOCKET_ERROR;
        }
        return original(socket, message, flags, sent, overlapped, routine);
    }

    const int written = proxy->SendDatagram(socket, target, message->lpBuffers, message->dwBufferCount);
    if (written == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }

    if (overlapped != nullptr)
    {
        appbox::network::CompleteOverlapped(socket, overlapped, static_cast<DWORD>(written), 0);
        WSASetLastError(WSA_IO_PENDING);
        return SOCKET_ERROR;
    }

    if (sent != nullptr)
    {
        *sent = static_cast<DWORD>(written);
    }
    return 0;
}

INT WSAAPI appbox::network::SandboxWSARecvMsg(SOCKET socket, LPWSAMSG message, LPDWORD received,
                                              LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    appbox::network::Proxy* proxy = appbox::network::ProxyOfProcess();
    if (proxy == nullptr || message == nullptr || !proxy->ProxiesDatagram(socket))
    {
        const RecvMsgFn original = OriginalWSARecvMsg();
        if (original == nullptr)
        {
            WSASetLastError(WSAEOPNOTSUPP);
            return SOCKET_ERROR;
        }
        return original(socket, message, received, overlapped, routine);
    }

    const int family = FamilyOf(message);

    if (overlapped != nullptr)
    {
        if (!appbox::network::QueueDatagramReceive(socket, message->lpBuffers, message->dwBufferCount,
                                                   message->name != nullptr, message->name, &message->namelen,
                                                   overlapped, family))
        {
            WSASetLastError(WSAENOBUFS);
            return SOCKET_ERROR;
        }
        if (message->Control.buf != nullptr)
        {
            message->Control.len = 0;
        }
        WSASetLastError(WSA_IO_PENDING);
        return SOCKET_ERROR;
    }

    const DWORD                       deadline = GetTickCount() + 30000;
    appbox::network::socks5::Endpoint source;
    const int read = proxy->ReceiveDatagram(socket, message->lpBuffers, message->dwBufferCount, source, deadline);
    if (read == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }

    if (message->name != nullptr)
    {
        appbox::network::WriteAddress(source, family, message->name, &message->namelen);
    }
    if (message->Control.buf != nullptr)
    {
        message->Control.len = 0;
    }
    if (received != nullptr)
    {
        *received = static_cast<DWORD>(read);
    }
    return 0;
}
