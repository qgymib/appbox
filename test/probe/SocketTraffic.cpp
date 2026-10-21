#include "sandbox/utils/Winsock.hpp" /* Must be first include file */
#include "SocketTraffic.hpp"
#include "WString.hpp"
#include <cstring>

namespace
{

using Operation = appbox::test::ProtocolSocketTraffic::Operation;
using Result = appbox::test::ProtocolSocketTraffic::Result;
using Step = appbox::test::ProtocolSocketTraffic::Step;

/** Timeout of a receive which the step did not set. */
constexpr int kDefaultTimeoutMs = 2000;

/** Size of the buffer a step receives into. */
constexpr std::size_t kBufferSize = 4096;

/** Return code of a call which failed. */
constexpr int kSocketError = SOCKET_ERROR;

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
 * @brief Resolve an extension function of the socket API.
 *
 * The functions of the socket API which have no import library are resolved by
 * the protocol identifier of the extension, which is what a real application
 * does as well.
 *
 * @param[in] socket Socket of the probe.
 * @param[in] identifier Identifier of the extension function.
 * @return The address of the function, null when it could not be resolved.
 */
void* ResolveExtension(SOCKET socket, const GUID& identifier)
{
    void* address = nullptr;
    DWORD returned = 0;
    if (WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, const_cast<GUID*>(&identifier), sizeof(identifier),
                 &address, sizeof(address), &returned, nullptr, nullptr) == SOCKET_ERROR)
    {
        return nullptr;
    }
    return address;
}

/**
 * @brief Wait for an overlapped operation of the probe to complete.
 * @param[in] socket Socket of the operation.
 * @param[in,out] overlapped The operation.
 * @param[in] timeout_ms Timeout of the wait.
 * @param[out] bytes Number of the bytes the operation transferred.
 * @return true when the operation completed successfully.
 */
bool WaitOverlapped(SOCKET socket, OVERLAPPED& overlapped, int timeout_ms, DWORD& bytes)
{
    const DWORD wait = WSAWaitForMultipleEvents(1, &overlapped.hEvent, TRUE, static_cast<DWORD>(timeout_ms), FALSE);
    if (wait != WSA_WAIT_EVENT_0)
    {
        WSASetLastError(wait == WSA_WAIT_TIMEOUT ? WSAETIMEDOUT : WSAEINVAL);
        return false;
    }

    DWORD flags = 0;
    if (!WSAGetOverlappedResult(socket, &overlapped, &bytes, FALSE, &flags))
    {
        return false;
    }
    return true;
}

/**
 * @brief Initialize the socket library of the probe process.
 *
 * The library is initialized once per process; every call which follows the
 * first one reports the version which is already running.
 */
void EnsureWinsock()
{
    static const bool initialized = []() {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)initialized;
}

/**
 * @brief Build the address a step names.
 * @param[in] step Step of the probe.
 * @param[out] address The address of the step, untouched on failure.
 * @return true when the address could be built.
 */
bool StepAddress(const Step& step, sockaddr_in& address)
{
    ZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(step.port);

    const std::wstring text = appbox::UTF8ToWide(step.address);
    return InetPtonW(AF_INET, text.c_str(), &address.sin_addr) == 1;
}

/**
 * @brief Format an address of the application.
 * @param[in] address The address to format.
 * @return The text of the address, `address:port`.
 */
std::string FormatAddress(const sockaddr_in& address)
{
    char text[INET_ADDRSTRLEN] = {};
    if (InetNtopA(AF_INET, &address.sin_addr, text, sizeof(text)) == nullptr)
    {
        return std::string();
    }
    return std::string(text) + ":" + std::to_string(ntohs(address.sin_port));
}

/**
 * @brief Set the timeout of a receive of a socket.
 * @param[in] socket Socket to configure.
 * @param[in] timeout_ms Timeout in milliseconds.
 */
void SetTimeout(SOCKET socket, int timeout_ms)
{
    const DWORD timeout = static_cast<DWORD>(timeout_ms > 0 ? timeout_ms : kDefaultTimeoutMs);
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
}

/**
 * @brief Report a failure of a call.
 * @param[out] result Result of the step.
 */
void ReportFailure(Result& result)
{
    result.code = kSocketError;
    result.error = WSAGetLastError();
}

/**
 * @brief Open a datagram socket of the probe on the loopback address.
 * @param[in] step Step of the probe.
 * @param[out] result Result of the step, set when the socket cannot be opened.
 * @return The socket, `INVALID_SOCKET` on failure.
 */
SOCKET OpenDatagramSocket(const Step& step, Result& result)
{
    const SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET)
    {
        ReportFailure(result);
        return INVALID_SOCKET;
    }

    sockaddr_in local;
    ZeroMemory(&local, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = 0;
    if (bind(socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR)
    {
        ReportFailure(result);
        closesocket(socket);
        return INVALID_SOCKET;
    }

    SetTimeout(socket, step.timeout_ms);
    return socket;
}

/**
 * @brief Perform the steps of a datagram socket which is connected first.
 * @param[in] step Step of the probe.
 * @param[out] result Result of the step.
 */
void RunConnectedDatagramStep(const Step& step, Result& result)
{
    const SOCKET socket = OpenDatagramSocket(step, result);
    if (socket == INVALID_SOCKET)
    {
        return;
    }

    sockaddr_in target;
    if (!StepAddress(step, target))
    {
        result.code = kSocketError;
        result.error = WSAEINVAL;
        closesocket(socket);
        return;
    }

    if (connect(socket, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == SOCKET_ERROR)
    {
        ReportFailure(result);
        closesocket(socket);
        return;
    }

    const int sent = send(socket, step.payload.data(), static_cast<int>(step.payload.size()), 0);
    if (sent == SOCKET_ERROR)
    {
        ReportFailure(result);
        closesocket(socket);
        return;
    }

    result.code = 0;
    result.sent = sent;

    char      buffer[kBufferSize] = {};
    const int received = recv(socket, buffer, sizeof(buffer), 0);
    if (received == SOCKET_ERROR)
    {
        ReportFailure(result);
    }
    else
    {
        result.received.assign(buffer, static_cast<std::size_t>(received));
        /* A connected socket reports the address it was connected to. */
        result.source = FormatAddress(target);
    }
    closesocket(socket);
}

/**
 * @brief Perform the steps of a datagram socket through the message functions.
 * @param[in] step Step of the probe.
 * @param[out] result Result of the step.
 */
void RunSendMsgStep(const Step& step, Result& result)
{
    const SOCKET socket = OpenDatagramSocket(step, result);
    if (socket == INVALID_SOCKET)
    {
        return;
    }

    const auto send_msg = reinterpret_cast<SendMsgFn>(ResolveExtension(socket, kWsaIdWSASendMsg));
    const auto recv_msg = reinterpret_cast<RecvMsgFn>(ResolveExtension(socket, kWsaIdWSARecvMsg));
    if (send_msg == nullptr || recv_msg == nullptr)
    {
        result.code = kSocketError;
        result.error = WSAEOPNOTSUPP;
        closesocket(socket);
        return;
    }

    sockaddr_in target;
    if (!StepAddress(step, target))
    {
        result.code = kSocketError;
        result.error = WSAEINVAL;
        closesocket(socket);
        return;
    }

    WSABUF payload;
    payload.buf = const_cast<char*>(step.payload.data());
    payload.len = static_cast<ULONG>(step.payload.size());

    WSAMSG message;
    ZeroMemory(&message, sizeof(message));
    message.name = reinterpret_cast<LPSOCKADDR>(&target);
    message.namelen = sizeof(target);
    message.lpBuffers = &payload;
    message.dwBufferCount = 1;

    DWORD sent = 0;
    if (send_msg(socket, &message, 0, &sent, nullptr, nullptr) == SOCKET_ERROR)
    {
        ReportFailure(result);
        closesocket(socket);
        return;
    }

    result.code = 0;
    result.sent = static_cast<int>(sent);

    char   buffer[kBufferSize] = {};
    WSABUF answer;
    answer.buf = buffer;
    answer.len = sizeof(buffer);

    sockaddr_in from;
    ZeroMemory(&from, sizeof(from));
    int from_length = sizeof(from);

    WSAMSG reply;
    ZeroMemory(&reply, sizeof(reply));
    reply.name = reinterpret_cast<LPSOCKADDR>(&from);
    reply.namelen = from_length;
    reply.lpBuffers = &answer;
    reply.dwBufferCount = 1;

    DWORD received = 0;
    if (recv_msg(socket, &reply, &received, nullptr, nullptr) == SOCKET_ERROR)
    {
        ReportFailure(result);
    }
    else
    {
        result.received.assign(buffer, static_cast<std::size_t>(received));
        result.source = FormatAddress(*reinterpret_cast<const sockaddr_in*>(reply.name));
    }
    closesocket(socket);
}

/**
 * @brief Perform the steps of a datagram socket as overlapped operations.
 * @param[in] step Step of the probe.
 * @param[out] result Result of the step.
 */
void RunOverlappedDatagramStep(const Step& step, Result& result)
{
    const SOCKET socket = OpenDatagramSocket(step, result);
    if (socket == INVALID_SOCKET)
    {
        return;
    }

    sockaddr_in target;
    if (!StepAddress(step, target))
    {
        result.code = kSocketError;
        result.error = WSAEINVAL;
        closesocket(socket);
        return;
    }

    OVERLAPPED sending;
    ZeroMemory(&sending, sizeof(sending));
    sending.hEvent = WSACreateEvent();

    WSABUF payload;
    payload.buf = const_cast<char*>(step.payload.data());
    payload.len = static_cast<ULONG>(step.payload.size());

    DWORD sent = 0;
    int   status = WSASendTo(socket, &payload, 1, &sent, 0, reinterpret_cast<const sockaddr*>(&target), sizeof(target),
                             &sending, nullptr);
    if (status == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING || !WaitOverlapped(socket, sending, step.timeout_ms, sent))
        {
            ReportFailure(result);
            WSACloseEvent(sending.hEvent);
            closesocket(socket);
            return;
        }
    }

    result.code = 0;
    result.sent = static_cast<int>(sent);

    char   buffer[kBufferSize] = {};
    WSABUF answer;
    answer.buf = buffer;
    answer.len = sizeof(buffer);

    sockaddr_in from;
    ZeroMemory(&from, sizeof(from));
    int from_length = sizeof(from);

    OVERLAPPED receiving;
    ZeroMemory(&receiving, sizeof(receiving));
    receiving.hEvent = WSACreateEvent();

    DWORD received = 0;
    DWORD flags = 0;
    status = WSARecvFrom(socket, &answer, 1, &received, &flags, reinterpret_cast<sockaddr*>(&from), &from_length,
                         &receiving, nullptr);
    if (status == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING || !WaitOverlapped(socket, receiving, step.timeout_ms, received))
        {
            ReportFailure(result);
            WSACloseEvent(receiving.hEvent);
            WSACloseEvent(sending.hEvent);
            closesocket(socket);
            return;
        }
    }

    result.received.assign(buffer, static_cast<std::size_t>(received));
    result.source = FormatAddress(from);

    WSACloseEvent(receiving.hEvent);
    WSACloseEvent(sending.hEvent);
    closesocket(socket);
}

/**
 * @brief Perform the steps which work with a datagram socket.
 * @param[in] step Step of the probe.
 * @param[out] result Result of the step.
 */
void RunDatagramStep(const Step& step, Result& result)
{
    const SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET)
    {
        ReportFailure(result);
        return;
    }

    sockaddr_in local;
    ZeroMemory(&local, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = 0;
    if (bind(socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR)
    {
        ReportFailure(result);
        closesocket(socket);
        return;
    }
    SetTimeout(socket, step.timeout_ms);

    sockaddr_in target;
    if (!StepAddress(step, target))
    {
        result.code = kSocketError;
        result.error = WSAEINVAL;
        closesocket(socket);
        return;
    }

    const int sent = sendto(socket, step.payload.data(), static_cast<int>(step.payload.size()), 0,
                            reinterpret_cast<const sockaddr*>(&target), sizeof(target));
    if (sent == SOCKET_ERROR)
    {
        ReportFailure(result);
        closesocket(socket);
        return;
    }

    result.code = 0;
    result.sent = sent;

    if (step.operation == Operation::UdpSendOnly)
    {
        closesocket(socket);
        return;
    }

    char        buffer[kBufferSize] = {};
    sockaddr_in from;
    int         from_length = sizeof(from);
    ZeroMemory(&from, sizeof(from));

    const int received = recvfrom(socket, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &from_length);
    if (received == SOCKET_ERROR)
    {
        ReportFailure(result);
    }
    else
    {
        result.received.assign(buffer, static_cast<std::size_t>(received));
        result.source = FormatAddress(from);
    }
    closesocket(socket);
}

/**
 * @brief Perform the steps of a stream socket through the extension function.
 * @param[in] step Step of the probe.
 * @param[out] result Result of the step.
 */
void RunConnectExStep(const Step& step, Result& result)
{
    const SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET)
    {
        ReportFailure(result);
        return;
    }

    /*
     * The extension function requires a socket which is bound, which is what
     * the documentation of the function asks of its caller.
     */
    sockaddr_in local;
    ZeroMemory(&local, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = 0;
    if (bind(socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR)
    {
        ReportFailure(result);
        closesocket(socket);
        return;
    }

    const auto connect_ex = reinterpret_cast<ConnectExFn>(ResolveExtension(socket, kWsaIdConnectEx));
    if (connect_ex == nullptr)
    {
        result.code = kSocketError;
        result.error = WSAEOPNOTSUPP;
        closesocket(socket);
        return;
    }

    sockaddr_in target;
    if (!StepAddress(step, target))
    {
        result.code = kSocketError;
        result.error = WSAEINVAL;
        closesocket(socket);
        return;
    }

    OVERLAPPED overlapped;
    ZeroMemory(&overlapped, sizeof(overlapped));
    overlapped.hEvent = WSACreateEvent();

    DWORD sent = 0;
    if (!connect_ex(socket, reinterpret_cast<const sockaddr*>(&target), sizeof(target), nullptr, 0, &sent, &overlapped))
    {
        if (WSAGetLastError() != WSA_IO_PENDING || !WaitOverlapped(socket, overlapped, step.timeout_ms, sent))
        {
            ReportFailure(result);
            WSACloseEvent(overlapped.hEvent);
            closesocket(socket);
            return;
        }
    }
    WSACloseEvent(overlapped.hEvent);

    SetTimeout(socket, step.timeout_ms);
    const int written = send(socket, step.payload.data(), static_cast<int>(step.payload.size()), 0);
    if (written == SOCKET_ERROR)
    {
        ReportFailure(result);
        closesocket(socket);
        return;
    }

    result.code = 0;
    result.sent = written;

    char      buffer[kBufferSize] = {};
    const int received = recv(socket, buffer, sizeof(buffer), 0);
    if (received == SOCKET_ERROR)
    {
        ReportFailure(result);
    }
    else
    {
        result.received.assign(buffer, static_cast<std::size_t>(received));
    }
    closesocket(socket);
}

/**
 * @brief Perform the steps which report the address a socket names as its peer.
 * @param[in] step Step of the probe.
 * @param[out] result Result of the step.
 */
void RunPeerNameStep(const Step& step, Result& result)
{
    const SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET)
    {
        ReportFailure(result);
        return;
    }

    sockaddr_in target;
    if (!StepAddress(step, target))
    {
        result.code = kSocketError;
        result.error = WSAEINVAL;
        closesocket(socket);
        return;
    }

    if (connect(socket, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == SOCKET_ERROR)
    {
        ReportFailure(result);
        closesocket(socket);
        return;
    }

    sockaddr_in peer;
    ZeroMemory(&peer, sizeof(peer));
    int length = sizeof(peer);
    if (getpeername(socket, reinterpret_cast<sockaddr*>(&peer), &length) == SOCKET_ERROR)
    {
        ReportFailure(result);
        closesocket(socket);
        return;
    }

    result.code = 0;
    result.peer = FormatAddress(peer);
    closesocket(socket);
}

/**
 * @brief Perform the steps which work with a stream socket.
 * @param[in] step Step of the probe.
 * @param[out] result Result of the step.
 */
void RunStreamStep(const Step& step, Result& result)
{
    const SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET)
    {
        ReportFailure(result);
        return;
    }

    if (step.operation == Operation::TcpConnectNonBlocking)
    {
        u_long mode = 1;
        if (ioctlsocket(socket, FIONBIO, &mode) == SOCKET_ERROR)
        {
            ReportFailure(result);
            closesocket(socket);
            return;
        }
    }

    sockaddr_in target;
    if (!StepAddress(step, target))
    {
        result.code = kSocketError;
        result.error = WSAEINVAL;
        closesocket(socket);
        return;
    }

    if (connect(socket, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == SOCKET_ERROR)
    {
        ReportFailure(result);
        closesocket(socket);
        return;
    }
    result.code = 0;

    if (step.operation == Operation::TcpConnectNonBlocking)
    {
        /*
         * The socket has to be non-blocking once the connection returned: a
         * receive without an answer reports the mode, and the timeout keeps
         * the check from blocking when the mode was not restored.
         */
        SetTimeout(socket, step.timeout_ms);
        char      byte = 0;
        const int received = recv(socket, &byte, 1, 0);
        const int error = WSAGetLastError();

        result.nonblocking = received == SOCKET_ERROR && error == WSAEWOULDBLOCK;
        result.error = received == SOCKET_ERROR ? error : 0;
        closesocket(socket);
        return;
    }

    if (step.operation == Operation::TcpConnect)
    {
        closesocket(socket);
        return;
    }

    SetTimeout(socket, step.timeout_ms);
    const int sent = send(socket, step.payload.data(), static_cast<int>(step.payload.size()), 0);
    if (sent == SOCKET_ERROR)
    {
        ReportFailure(result);
        closesocket(socket);
        return;
    }
    result.sent = sent;

    char      buffer[kBufferSize] = {};
    const int received = recv(socket, buffer, sizeof(buffer), 0);
    if (received == SOCKET_ERROR)
    {
        ReportFailure(result);
    }
    else
    {
        result.received.assign(buffer, static_cast<std::size_t>(received));
    }
    closesocket(socket);
}

/**
 * @brief Perform one step of the probe.
 * @param[in] step Step of the probe.
 * @return The result of the step.
 */
Result RunStep(const Step& step)
{
    EnsureWinsock();

    Result result;
    switch (step.operation)
    {
    case Operation::UdpEcho:
    case Operation::UdpSendOnly:
        RunDatagramStep(step, result);
        break;
    case Operation::UdpConnectedEcho:
        RunConnectedDatagramStep(step, result);
        break;
    case Operation::UdpSendMsg:
        RunSendMsgStep(step, result);
        break;
    case Operation::UdpOverlappedEcho:
        RunOverlappedDatagramStep(step, result);
        break;
    case Operation::TcpConnectEx:
        RunConnectExStep(step, result);
        break;
    case Operation::TcpPeerName:
        RunPeerNameStep(step, result);
        break;
    case Operation::TcpEcho:
    case Operation::TcpConnect:
    case Operation::TcpConnectNonBlocking:
    default:
        RunStreamStep(step, result);
        break;
    }
    return result;
}

} // namespace

static nlohmann::json ProbeSocketTraffic_Entry(const nlohmann::json& data)
{
    const auto req = data.get<appbox::test::ProtocolSocketTraffic::Req>();

    appbox::test::ProtocolSocketTraffic::Rsp rsp;
    rsp.results.reserve(req.steps.size());
    for (const auto& step : req.steps)
    {
        rsp.results.push_back(RunStep(step));
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeSocketTraffic("SocketTraffic", ProbeSocketTraffic_Entry);
