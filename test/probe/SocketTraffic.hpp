#ifndef APPBOX_TEST_PROBE_SOCKET_TRAFFIC_HPP
#define APPBOX_TEST_PROBE_SOCKET_TRAFFIC_HPP

#include "probe/__init__.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::test
{

/**
 * @brief Protocol of the socket probe of the end to end cases of the proxy.
 *
 * The probe runs inside the sandboxed process and performs the socket calls a
 * case asks for, so a case pins what the sandbox does with a call of the
 * application instead of what a helper of the test does.
 */
struct ProtocolSocketTraffic
{
    /**
     * @brief Operation a step performs.
     */
    enum class Operation
    {
        TcpEcho,               /* Connect, send a payload and read the echo. */
        TcpConnect,            /* Connect and report the result only. */
        TcpConnectNonBlocking, /* Connect on a non-blocking socket and report the mode afterwards. */
        UdpEcho,               /* Send a datagram and read the answer. */
        UdpSendOnly,           /* Send a datagram without waiting for an answer. */
    };

    /**
     * @brief One socket call the probe performs.
     */
    struct Step
    {
        /**
         * @brief Operation of the step.
         */
        Operation operation = Operation::TcpEcho;

        /**
         * @brief Address literal of the target, in UTF-8.
         */
        std::string address;

        /**
         * @brief Port of the target.
         */
        std::uint16_t port = 0;

        /**
         * @brief Payload the step sends.
         */
        std::string payload;

        /**
         * @brief Timeout of a receive, in milliseconds.
         */
        int timeout_ms = 2000;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Step, operation, address, port, payload, timeout_ms)
    };

    /**
     * @brief Result of one step.
     */
    struct Result
    {
        /**
         * @brief Return code of the last call, zero on success and `SOCKET_ERROR` on failure.
         */
        int code = 0;

        /**
         * @brief Error of the last call, `WSAGetLastError()`, zero on success.
         */
        int error = 0;

        /**
         * @brief Number of the bytes a send handed over.
         */
        int sent = 0;

        /**
         * @brief Payload which was received.
         */
        std::string received;

        /**
         * @brief Address a datagram came from, `address:port`.
         */
        std::string source;

        /**
         * @brief Whether the socket was still non-blocking after a connection.
         */
        bool nonblocking = false;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Result, code, error, sent, received, source, nonblocking)
    };

    /**
     * @brief The steps of one probe call.
     */
    struct Req
    {
        std::vector<Step> steps;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, steps)
    };

    /**
     * @brief The results of one probe call.
     */
    struct Rsp
    {
        std::vector<Result> results; /* One result per step, in the order of the steps. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Rsp, results)
    };
};

/**
 * @brief Carry the traffic of an application through the socket API.
 */
extern Probe ProbeSocketTraffic;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_SOCKET_TRAFFIC_HPP
