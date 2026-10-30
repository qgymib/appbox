#ifndef APPBOX_COMMON_REMOTE_CONNECTOR_HPP
#define APPBOX_COMMON_REMOTE_CONNECTOR_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <spdlog/spdlog.h>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include "RpcCodec.hpp"
#include "RemoteSession.hpp"

namespace appbox
{

/**
 * @brief Connector of a named pipe: the end which opens the name of the pipe.
 *
 * The connector opens the client end of a pipe and hands it to its caller as a
 * `RemoteSession`. It owns the retries of the connection only: the session owns
 * the handle and the thread which serves it, so the connector may be released
 * while the session keeps working.
 *
 * The connector knows no method either: what the peer serves is the business of
 * the session, which is the same type the accepter of the other end created.
 */
class RemoteConnector
{
public:
    typedef std::shared_ptr<RemoteConnector> Ptr;

    ~RemoteConnector();

    /**
     * @brief Create a connector for the given named pipe path.
     *
     * The connector opens nothing yet, so a caller may create it long before the
     * pipe exists.
     *
     * @param[in] pipe_path Full path of the pipe.
     * @return The new connector instance.
     */
    static Ptr Create(const std::string& pipe_path);

    /**
     * @brief Open the pipe and return the session which serves it.
     *
     * A pipe which does not exist yet, which is busy or which was just closed is
     * retried until the budget is used up, so a client may start while the
     * server is still starting. The session starts reading before the call
     * returns, so a peer may send a request at once.
     *
     * @param[in] timeout_ms Budget of the connection attempts.
     * @return The connected session, null when the pipe cannot be opened.
     */
    RemoteSession::Ptr Connect(std::uint32_t timeout_ms = 5000);

    /**
     * @brief Process id of the server at the other end of the pipe.
     * @return Process id of the server, zero when it is unknown.
     */
    DWORD ServerProcessId() const;

private:
    RemoteConnector();

    /**
     * @brief Internal state of the connector.
     */
    struct Data;

    std::shared_ptr<Data> data_;
};

} // namespace appbox

/*
 * The internal state of the connector and the members of the class are its
 * implementation, so they live in a detail header which only this header
 * includes.
 */
#include "details/RemoteConnectorDetail.hpp"

#endif // APPBOX_COMMON_REMOTE_CONNECTOR_HPP
