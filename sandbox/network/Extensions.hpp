#ifndef APPBOX_SANDBOX_NETWORK_EXTENSIONS_HPP
#define APPBOX_SANDBOX_NETWORK_EXTENSIONS_HPP

#include "utils/Winsock.hpp"

namespace appbox
{
namespace network
{

/**
 * @brief The functions of the socket API which have no import library.
 *
 * `ConnectEx`, `WSASendMsg` and `WSARecvMsg` are not exported by a library an
 * application links against: it asks `WSAIoctl` for their addresses with the
 * identifier of the extension. The sandbox answers that question with its own
 * implementation (see `hook/WSAIoctl.cpp`), which is how a connection, a
 * datagram and a message of a program which resolves them are carried.
 *
 * Each function forwards to the function of the system when the proxy does not
 * apply, so an application which works without a proxy keeps the path of the
 * host.
 */

/**
 * @brief Connect a socket to a target through the proxy.
 *
 * The handshake of the proxy is performed before the call returns, and the
 * operation is then completed as an overlapped one, which is the shape the
 * caller of the function expects: the result is written into the `OVERLAPPED`
 * of the caller, its event is signalled and a completion packet is queued.
 *
 * @param[in] socket Socket to connect, which has to be bound.
 * @param[in] name Address and port of the target.
 * @param[in] namelen Number of the bytes of the address.
 * @param[in] send_buffer Payload which is sent once the socket is connected.
 * @param[in] send_length Number of the bytes of the payload.
 * @param[out] bytes_sent Number of the bytes which were sent.
 * @param[in,out] overlapped The operation of the caller.
 * @return true on success, false with the last error set. A call which started
 *         the operation reports `WSA_IO_PENDING`.
 */
BOOL PASCAL SandboxConnectEx(SOCKET socket, const sockaddr* name, int namelen, PVOID send_buffer, DWORD send_length,
                             LPDWORD bytes_sent, LPOVERLAPPED overlapped);

/**
 * @brief Send a message of a datagram socket through the proxy.
 * @param[in] socket Socket to send with.
 * @param[in] message Message of the caller.
 * @param[in] flags Flags of the call, which are not used by the proxy.
 * @param[out] sent Number of the payload bytes which were sent.
 * @param[in,out] overlapped The operation of the caller, may be null.
 * @param[in] routine Completion routine of the caller, which is not used by the
 *                    proxy because the operation completes before the call
 *                    returns.
 * @return 0 on success, `SOCKET_ERROR` on failure with the last error set.
 */
INT WSAAPI SandboxWSASendMsg(SOCKET socket, LPWSAMSG message, DWORD flags, LPDWORD sent, LPWSAOVERLAPPED overlapped,
                             LPWSAOVERLAPPED_COMPLETION_ROUTINE routine);

/**
 * @brief Receive a message of a datagram socket through the proxy.
 *
 * An overlapped call is served by the worker of the sandbox, which waits for
 * the datagram of the relay and fills the buffers of the caller when it
 * arrives.
 *
 * @param[in] socket Socket to receive from.
 * @param[in] message Message of the caller.
 * @param[out] received Number of the payload bytes which were received.
 * @param[in,out] overlapped The operation of the caller, may be null.
 * @param[in] routine Completion routine of the caller, which is not used by the
 *                    proxy.
 * @return 0 on success, `SOCKET_ERROR` on failure with the last error set.
 */
INT WSAAPI SandboxWSARecvMsg(SOCKET socket, LPWSAMSG message, LPDWORD received, LPWSAOVERLAPPED overlapped,
                             LPWSAOVERLAPPED_COMPLETION_ROUTINE routine);

} // namespace network
} // namespace appbox

#endif // APPBOX_SANDBOX_NETWORK_EXTENSIONS_HPP
