#ifndef APPBOX_COMMON_REMOTE_ACCEPTER_HPP
#define APPBOX_COMMON_REMOTE_ACCEPTER_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
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
 * @brief Win32 code of a failed creation of a pipe server.
 *
 * The creation answers the code `CreateNamedPipe()` reported, because that code
 * is what a caller of the launcher forwards to its own peer and what it names
 * in the report of a failed start. Two of the codes have a meaning the creation
 * alone can not decide: `ERROR_ACCESS_DENIED` is the answer of the first
 * instance both for a name which another server of this user holds and for a
 * name this process may not use. The caller tells them apart with
 * `detail::AccepterPeerIsReachable()`, which the header of the detail lives in.
 */
typedef DWORD Win32ErrorCode;

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
 * The accepter owns a private thread which creates the instances of the pipe and
 * waits for the clients, so the caller of the methods above is never that
 * thread. Every accepted connection is handed to a session which owns a thread
 * of its own, so the sessions of one accepter are independent of each other and
 * of the accepter: a client which blocks its session blocks no other client.
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
     * @brief Create an accepter for the given named pipe path.
     *
     * The first instance of the pipe is created before the function returns, so
     * a caller which shares the pipe name with another process learns whether it
     * is the server or a client.
     *
     * @param[in] pipe Name of the pipe, for example \\.\pipe\appbox.
     * @return The new accepter instance, or the Win32 code of the failed
     *         creation.
     */
    static tl::expected<Ptr, Win32ErrorCode> Create(const std::string& pipe) noexcept;

    /**
     * @brief Race for the given named pipe path.
     *
     * The first instance of the pipe is created with
     * `FILE_FLAG_FIRST_PIPE_INSTANCE`, so the answer of the call tells whether
     * this process won the race:
     * - a value is the accepter of this process, which now owns the name;
     * - `ERROR_ACCESS_DENIED` is a name whose first instance another process
     *   created. The caller asks `detail::AccepterPeerIsReachable()` whether a
     *   live server of this user holds the name - it may hand its request over
     *   then - or whether the name belongs to another user;
     * - any other code is a pipe which cannot be created at all.
     *
     * @param[in] pipe Name of the pipe, for example \\.\pipe\appbox.
     * @param[in] options Options of the accepter.
     * @return The new accepter instance, or the Win32 code of the failed
     *         creation.
     */
    static tl::expected<Ptr, Win32ErrorCode> CreateCompetitive(const std::string& pipe,
                                                               const Options&     options = Options()) noexcept;

    virtual ~RemoteAccepter();

    /**
     * @brief Set the callback which receives every accepted connection.
     *
     * The callback runs on the thread of the accepter before the session starts
     * reading, so it is the place which registers the methods of the session.
     * It must not block and must not throw.
     *
     * The session is owned by the accepter only while it is connected: a caller
     * which keeps a session alive after its connection ended keeps the handle of
     * the pipe alive as well, so a handler which needs the session later holds a
     * weak pointer.
     *
     * @param[in] callback Callback to set, an empty function removes it.
     */
    void SetSessionCallback(SessionCallback callback) noexcept;

    /**
     * @brief Start the pipe server and begin listening for incoming connections.
     * @return Success when the accepter is listening; `kRemoteEventCreateFailed`
     *         when an event the accept thread waits on does not exist;
     *         `kRemoteThreadStartFailed` when the accept thread cannot be
     *         started; `kInvalidState` when the accepter already stopped.
     */
    tl::expected<void, ErrorCode> Start() noexcept;

    /**
     * @brief Stop accepting connections and end the thread of the accepter.
     *
     * New connections are refused, the requests which are already handled are
     * answered, and the sessions are released. The call is idempotent and may be
     * repeated after the accepter stopped.
     */
    void Stop() noexcept;

    /**
     * @brief Set the callback which receives a fatal failure of the accept loop.
     *
     * The callback runs on the thread of the accepter. It must not block and
     * must not throw.
     *
     * @param[in] callback Callback to set, an empty function removes it.
     */
    void SetAcceptFailureCallback(AcceptFailureCallback callback) noexcept;

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
