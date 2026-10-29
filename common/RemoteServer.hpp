#ifndef APPBOX_COMMON_REMOTE_SERVER_HPP
#define APPBOX_COMMON_REMOTE_SERVER_HPP

#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "RpcCodec.hpp"
#include "RemoteSession.hpp"

namespace appbox
{

/**
 * @brief Outcome of the competitive creation of a pipe server.
 *
 * Two launchers which carry the same sandbox identifier race for the same pipe
 * name, so the creation has to tell the winner from the loser and from a run
 * which is not allowed to use the pipe at all.
 */
enum class ServerCreateStatus
{
    Created,        /* This process created the first instance of the pipe. */
    AlreadyRunning, /* Another server listens on the pipe. */
    AccessDenied,   /* The name is taken but the pipe cannot be connected. */
    Failed,         /* The instance cannot be created, see the error code. */
};

/**
 * @brief Kind of a failure of the accept loop of a pipe server.
 */
enum class AcceptFailureKind
{
    StillServing, /* Another instance still serves, the creation is retried. */
    NoInstance,   /* No instance serves any more, the server is dead. */
};

/**
 * @brief JSON-RPC server listening on a named pipe.
 *
 * The server owns a private IO thread which accepts pipe connections and
 * dispatches incoming requests to registered method callbacks. A malformed
 * request is answered with a JSON-RPC error and never ends the IO thread.
 */
class RemoteServer : public std::enable_shared_from_this<RemoteServer>
{
public:
    typedef std::function<void(uint64_t, const nlohmann::json&)> MethodCallback;
    typedef std::function<void(AcceptFailureKind, DWORD)>        AcceptFailureCallback;
    typedef std::shared_ptr<RemoteServer>                        Ptr;
    typedef std::weak_ptr<RemoteServer>                          WeakPtr;

    /**
     * @brief Options of the pipe server.
     */
    struct Options
    {
        /* Create the first instance with FILE_FLAG_FIRST_PIPE_INSTANCE. */
        bool first_instance = false;

        /* Restrict the DACL of the pipe to the user of this process. */
        bool restrict_to_current_user = true;

        /* Largest request payload which is accepted, in bytes. */
        std::size_t max_request_size = kMaxRpcPayloadSize;

        /*
         * Error the creation of a follow-up instance reports instead of
         * creating the instance, zero to create it. The retry of a failed
         * creation cannot be triggered otherwise, so the unit tests of the
         * accept loop set it.
         */
        std::function<DWORD()> create_instance_error;
    };

    /**
     * @brief Result of CreateCompetitive().
     */
    struct CreateResult
    {
        ServerCreateStatus status = ServerCreateStatus::Failed; /* Outcome. */
        DWORD              error_code = 0;                      /* Win32 code of a failure. */
        Ptr                server;                              /* Set when the status is Created. */
    };

    /**
     * @brief Create a server for the given named pipe path.
     *
     * The first instance of the pipe is created before the function returns,
     * so a caller which shares the pipe name with another process learns
     * whether it is the server or a client.
     *
     * @param[in] pipe Name of the pipe, for example \\.\pipe\appbox.
     * @return The new server instance.
     * @throw std::system_error The instance of the pipe cannot be created.
     */
    static Ptr Create(const std::string& pipe);

    /**
     * @brief Race for the given named pipe path.
     * @param[in] pipe Name of the pipe, for example \\.\pipe\appbox.
     * @param[in] options Options of the server.
     * @return The outcome of the creation and, when this process won the race,
     *         the new server instance.
     */
    static CreateResult CreateCompetitive(const std::string& pipe, const Options& options = Options());

    virtual ~RemoteServer();

    /**
     * @brief Register a method callback for handling JSON requests.
     * @param[in] method Method name to register.
     * @param[in] callback Callback function to handle the method.
     */
    void RegisterMethod(const std::string& method, MethodCallback callback);

    /**
     * @brief Start the pipe server and begin listening for incoming connections.
     * @return true when the server is listening, otherwise false.
     */
    bool Start();

    /**
     * @brief Stop accepting connections and end the IO thread.
     *
     * New connections are refused, the requests which are already handled are
     * answered, and the sessions are released. The call is idempotent and may
     * be repeated after the server stopped.
     */
    void Stop();

    /**
     * @brief Set the callback which receives a fatal failure of the accept loop.
     *
     * The callback runs on the IO thread of the server. It must not block and
     * must not throw.
     *
     * @param[in] callback Callback to set, an empty function removes it.
     */
    void SetAcceptFailureCallback(AcceptFailureCallback callback);

    /**
     * @brief Send a JSON response to a client.
     *
     * The call may be made from any thread, including the control thread which
     * handles the requests of the callbacks.
     *
     * @param[in] id Request ID.
     * @param[in] result JSON response data or error.
     */
    void SendResponse(uint64_t id, const RemoteResult& result);

    /**
     * @brief Query the process at the other end of the session of a request.
     *
     * The request has to be answered after the query, because the answer
     * releases the session of the request.
     *
     * @param[in] request_id Id of the request.
     * @return Process id of the client, zero when it is unknown.
     */
    DWORD PeerProcessIdOfRequest(uint64_t request_id) const;

private:
    RemoteServer();

    /**
     * @brief Internal state of the server.
     */
    struct Data;

    std::shared_ptr<Data> data_;
};

} // namespace appbox

/*
 * The internal state of the server and the members of the class are its
 * implementation, so they live in a detail header which only this header
 * includes.
 */
#include "details/RemoteServerDetail.hpp"

#endif // APPBOX_COMMON_REMOTE_SERVER_HPP
