#ifndef APPBOX_SANDBOX_NETWORK_COMPLETION_HPP
#define APPBOX_SANDBOX_NETWORK_COMPLETION_HPP

#include "utils/Winsock.hpp"
#include <cstddef>

namespace appbox
{
namespace network
{

/**
 * @brief Completion of an overlapped operation which the sandbox performed.
 *
 * A datagram which is sent or received as an overlapped operation cannot be
 * handed to the stack, because the payload of the protocol is not the payload
 * of the application: the sandbox performs the operation itself and reports its
 * result the way the stack would have reported it. The result is written into
 * the `OVERLAPPED` of the application, its event is signalled, and a completion
 * packet is queued to the completion port of the socket when it has one.
 */

/**
 * @brief Report the result of an overlapped operation.
 *
 * The call is made once per operation, and it is the only place which writes
 * the `OVERLAPPED` of the application. A failure is reported as
 * `STATUS_UNSUCCESSFUL`, because the error of the operation is not translated
 * into a status by the sandbox.
 *
 * @param[in] socket Socket the operation was made on.
 * @param[in,out] overlapped The operation of the application.
 * @param[in] bytes Number of the bytes the operation transferred.
 * @param[in] error Error of the operation, zero on success.
 */
void CompleteOverlapped(SOCKET socket, LPWSAOVERLAPPED overlapped, DWORD bytes, DWORD error);

/**
 * @brief Queue a datagram which is received by the worker of the sandbox.
 *
 * The buffers of the application stay valid until the operation completes,
 * which is what the contract of an overlapped operation guarantees, so the
 * worker keeps their addresses and fills them when the datagram arrives.
 *
 * @param[in] socket Socket to receive from.
 * @param[in] buffers Buffers of the application.
 * @param[in] count Number of the buffers of the application.
 * @param[in] report_source Whether the address of the sender is reported.
 * @param[out] from Buffer of the address of the sender, may be null.
 * @param[out] fromlen Size of the buffer of the address, may be null.
 * @param[in] overlapped The operation of the application.
 * @param[in] family Family of the address the socket works with.
 * @return true when the operation was queued, false when the queue is full.
 */
bool QueueDatagramReceive(SOCKET socket, const WSABUF* buffers, std::size_t count, bool report_source, sockaddr* from,
                          int* fromlen, LPWSAOVERLAPPED overlapped, int family);

} // namespace network
} // namespace appbox

#endif // APPBOX_SANDBOX_NETWORK_COMPLETION_HPP
