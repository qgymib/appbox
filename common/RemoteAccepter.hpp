#ifndef APPBOX_COMMON_REMOTE_ACCEPTER_HPP
#define APPBOX_COMMON_REMOTE_ACCEPTER_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <windows.h>
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include "RpcCodec.hpp"
#include "RemoteSession.hpp"

namespace appbox
{

/**
 * @brief Outcome of the competitive creation of a pipe.
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
 * @brief Accepter of a named pipe: the end which owns the name of the pipe.
 *
 * The accepter creates the instances of the pipe, accepts the connections and
 * hands every accepted connection to its owner as a `RemoteSession`. It does not
 * know a single method: what the peers ask for is the business of the session
 * the owner registers its methods on, which makes the two ends of a connection
 * interchangeable.
 *
 * The accepter owns a private IO thread which accepts connections and runs the
 * sessions it created, so the sessions of one accepter share one thread and the
 * caller of the methods above is never that thread.
 */
class RemoteAccepter : public std::enable_shared_from_this<RemoteAccepter>
{
public:
    typedef std::function<void(AcceptFailureKind, DWORD)> AcceptFailureCallback;
    typedef std::function<void(RemoteSession::Ptr)>       SessionCallback;
    typedef std::shared_ptr<RemoteAccepter>               Ptr;
    typedef std::weak_ptr<RemoteAccepter>                 WeakPtr;

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
        Ptr                accepter;                            /* Set when the status is Created. */
    };

    /**
     * @brief Create an accepter for the given named pipe path.
     *
     * The first instance of the pipe is created before the function returns, so
     * a caller which shares the pipe name with another process learns whether it
     * is the server or a client.
     *
     * @param[in] pipe Name of the pipe, for example \\.\pipe\appbox.
     * @return The new accepter instance.
     * @throw std::system_error The instance of the pipe cannot be created.
     */
    static Ptr Create(const std::string& pipe);

    /**
     * @brief Race for the given named pipe path.
     * @param[in] pipe Name of the pipe, for example \\.\pipe\appbox.
     * @param[in] options Options of the accepter.
     * @return The outcome of the creation and, when this process won the race,
     *         the new accepter instance.
     */
    static CreateResult CreateCompetitive(const std::string& pipe, const Options& options = Options());

    virtual ~RemoteAccepter();

    /**
     * @brief Set the callback which receives every accepted connection.
     *
     * The callback runs on the IO thread of the accepter before the session
     * starts reading, so it is the place which registers the methods of the
     * session. It must not block and must not throw.
     *
     * The session is owned by the accepter only while it is connected: a caller
     * which keeps a session alive after its connection ended keeps the handle of
     * the pipe alive as well, so a handler which needs the session later holds a
     * weak pointer.
     *
     * @param[in] callback Callback to set, an empty function removes it.
     */
    void SetSessionCallback(SessionCallback callback);

    /**
     * @brief Start the pipe server and begin listening for incoming connections.
     * @return true when the accepter is listening, otherwise false.
     */
    bool Start();

    /**
     * @brief Stop accepting connections and end the IO thread.
     *
     * New connections are refused, the requests which are already handled are
     * answered, and the sessions are released. The call is idempotent and may be
     * repeated after the accepter stopped.
     */
    void Stop();

    /**
     * @brief Set the callback which receives a fatal failure of the accept loop.
     *
     * The callback runs on the IO thread of the accepter. It must not block and
     * must not throw.
     *
     * @param[in] callback Callback to set, an empty function removes it.
     */
    void SetAcceptFailureCallback(AcceptFailureCallback callback);

private:
    RemoteAccepter();

    /**
     * @brief Internal state of the accepter.
     */
    struct Data;

    std::shared_ptr<Data> data_;
};

} // namespace appbox

/*
 * The internal state of the accepter and the members of the class are its
 * implementation, so they live in a detail header which only this header
 * includes.
 */
#include "details/RemoteAccepterDetail.hpp"

#endif // APPBOX_COMMON_REMOTE_ACCEPTER_HPP
