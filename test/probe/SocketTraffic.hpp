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
        TcpEcho,               ///< Connect, send a payload and read the echo.
        TcpConnect,            ///< Connect and report the result only.
        TcpConnectNonBlocking, ///< Connect on a non-blocking socket and report the mode afterwards.
        TcpConnectEx,          ///< Connect through the extension function of the socket API.
        TcpPeerName,           ///< Connect and report the address the socket names as its peer.
        UdpEcho,               ///< Send a datagram and read the answer.
        UdpSendOnly,           ///< Send a datagram without waiting for an answer.
        UdpConnectedEcho,      ///< Connect a datagram socket, then send and receive on it.
        UdpSendMsg,            ///< Send and receive a datagram through the message functions.
        UdpOverlappedEcho,     ///< Send and receive a datagram as overlapped operations.
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
         * @brief Address the socket names as its peer, `address:port`.
         */
        std::string peer;

        /**
         * @brief Whether the socket was still non-blocking after a connection.
         */
        bool nonblocking = false;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Result, code, error, sent, received, source, peer, nonblocking)
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
 * @brief The tokens of Operation, which the protocol carries as text.
 *
 * The first entry is the value the reader falls back to for a token it does not
 * know, so an unknown token connects, sends and reads the echo.
 */
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolSocketTraffic::Operation,
                             {
                                 { ProtocolSocketTraffic::Operation::TcpEcho,               "tcp_echo"            },
                                 { ProtocolSocketTraffic::Operation::TcpConnect,            "tcp_connect"         },
                                 { ProtocolSocketTraffic::Operation::TcpConnectNonBlocking,
                                  "tcp_connect_non_blocking"                                                      },
                                 { ProtocolSocketTraffic::Operation::TcpConnectEx,          "tcp_connect_ex"      },
                                 { ProtocolSocketTraffic::Operation::TcpPeerName,           "tcp_peer_name"       },
                                 { ProtocolSocketTraffic::Operation::UdpEcho,               "udp_echo"            },
                                 { ProtocolSocketTraffic::Operation::UdpSendOnly,           "udp_send_only"       },
                                 { ProtocolSocketTraffic::Operation::UdpConnectedEcho,      "udp_connected_echo"  },
                                 { ProtocolSocketTraffic::Operation::UdpSendMsg,            "udp_send_msg"        },
                                 { ProtocolSocketTraffic::Operation::UdpOverlappedEcho,     "udp_overlapped_echo" },
})

/**
 * @brief Carry the traffic of an application through the socket API.
 */
extern Probe ProbeSocketTraffic;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_SOCKET_TRAFFIC_HPP
