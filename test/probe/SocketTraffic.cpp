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
    if (step.operation == Operation::UdpEcho || step.operation == Operation::UdpSendOnly)
    {
        RunDatagramStep(step, result);
    }
    else
    {
        RunStreamStep(step, result);
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
