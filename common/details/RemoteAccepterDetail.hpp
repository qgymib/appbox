#ifndef APPBOX_COMMON_DETAILS_REMOTE_ACCEPTER_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_REMOTE_ACCEPTER_DETAIL_HPP

/*
 * Implementation of `common/RemoteAccepter.hpp`: the internal state of the
 * accepter, the pipe handling on the private IO thread and the members of the
 * class. The header is included by the public header and is not meant to be
 * included on its own.
 */

#include "RemoteAccepter.hpp"
#include <aclapi.h>
#include <sddl.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace appbox
{
namespace detail
{

/**
 * @brief DACL of the pipe of a sandbox.
 *
 * The descriptor is protected, so no access control entry of a parent object is
 * inherited. Full access is granted to SYSTEM, to the administrators and to the
 * owner of the object, which is the user of the launcher; every other user is
 * denied.
 */
constexpr const char* kRemoteAccepterPipeDacl = "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)";

/**
 * @brief DACL and mandatory label of the pipe of a sandbox.
 *
 * The label is the medium integrity level with the no write up policy, which is
 * the level a program of an interactive user runs at.
 *
 * The label belongs to the descriptor of the creation: a label which is set on
 * an instance afterwards with SetSecurityInfo() is refused by the object
 * manager, which reports ERROR_ACCESS_DENIED for the system access control
 * entry of a pipe handle.
 */
constexpr const char* kRemoteAccepterPipeSddl = "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)S:(ML;;NW;;;ME)";

/**
 * @brief Map an error of CreateNamedPipe() to the result of the creation.
 *
 * The first instance of a pipe is created with FILE_FLAG_FIRST_PIPE_INSTANCE,
 * so a name which is already taken is reported as ERROR_ACCESS_DENIED, exactly
 * like a name whose pipe denies the access of this process. The two cases are
 * told apart by the caller, which probes the pipe before it asks for the
 * mapping: a pipe which is held by a live server counts as reachable, including
 * a pipe whose instances all serve a client at that moment.
 *
 * @param[in] error Error code of CreateNamedPipe().
 * @param[in] peer_reachable Whether the pipe of that name is held by a live
 *                           server of this user.
 * @return The outcome of the creation.
 */
inline ServerCreateStatus ClassifyAccepterCreateError(DWORD error, bool peer_reachable)
{
    if (error == ERROR_SUCCESS)
    {
        return ServerCreateStatus::Created;
    }

    if (error == ERROR_ACCESS_DENIED)
    {
        return peer_reachable ? ServerCreateStatus::AlreadyRunning : ServerCreateStatus::AccessDenied;
    }

    return ServerCreateStatus::Failed;
}

/**
 * @brief Classify an error of the accept loop of the pipe server.
 *
 * The errors of the resource kind are transient: the instances which are
 * already connected keep serving the clients, so the creation of the next
 * instance is retried. Every other error means that no instance serves the
 * clients any more.
 *
 * @param[in] error Error code of the failed instance creation.
 * @return The kind of the failure.
 */
inline AcceptFailureKind ClassifyAccepterAcceptError(DWORD error)
{
    switch (error)
    {
    case ERROR_NO_SYSTEM_RESOURCES:
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_NOT_ENOUGH_QUOTA:
        return AcceptFailureKind::StillServing;
    default:
        return AcceptFailureKind::NoInstance;
    }
}

/**
 * @brief Test whether an accept error reports a client which left again.
 *
 * A client may connect and close before the server completed the connect, which
 * is what a launcher does when it probes whether a pipe of its sandbox is
 * served: the instance is consumed without a session, while the server itself
 * keeps working. The error of such a connection says nothing about the server,
 * so it is not a failure of the accept loop.
 *
 * @param[in] error Error code of the failed connect.
 * @return true when the connection was abandoned by the client.
 */
inline bool AccepterConnectionWasAbandoned(DWORD error)
{
    switch (error)
    {
    case ERROR_NO_DATA:
    case ERROR_PIPE_NOT_CONNECTED:
    case ERROR_BROKEN_PIPE:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Test whether a pipe of a name is held by a live server.
 *
 * The probe connects to the pipe and closes the connection at once. A pipe whose
 * instances all serve a client refuses the connection with `ERROR_PIPE_BUSY`,
 * which says nothing about the server: the name exists and the server waits for
 * the next client as soon as one of its sessions ended. Such a pipe is reachable
 * as well, because the server which holds it is alive: a probe which reported it
 * as unreachable would turn a server which is busy at that moment into a pipe of
 * another user.
 *
 * @param[in] pipe Name of the pipe.
 * @return true when a client end of the pipe could be opened or every instance
 *         of the pipe serves a client, false when the name carries no pipe which
 *         this user may use.
 */
inline bool AccepterPeerIsReachable(const std::string& pipe)
{
    const HANDLE h = ::CreateFileA(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(h);
        return true;
    }

    const DWORD error = ::GetLastError();
    SPDLOG_DEBUG("the pipe '{}' cannot be connected: {}", pipe, error);

    return error == ERROR_PIPE_BUSY;
}

/**
 * @brief Self-relative security descriptor which is released with LocalFree().
 */
class RemoteAccepterSecurityDescriptor
{
public:
    RemoteAccepterSecurityDescriptor() = default;

    ~RemoteAccepterSecurityDescriptor()
    {
        Reset();
    }

    RemoteAccepterSecurityDescriptor(const RemoteAccepterSecurityDescriptor&) = delete;
    RemoteAccepterSecurityDescriptor& operator=(const RemoteAccepterSecurityDescriptor&) = delete;

    /**
     * @brief Build the descriptor from a security descriptor definition string.
     * @param[in] sddl Definition string.
     * @return true on success, otherwise false.
     */
    bool Build(const char* sddl)
    {
        Reset();

        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!::ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl, SDDL_REVISION_1, &descriptor, nullptr))
        {
            return false;
        }

        descriptor_ = descriptor;
        return true;
    }

    /**
     * @brief Whether the descriptor was built.
     * @return true when the descriptor is usable.
     */
    bool Valid() const
    {
        return descriptor_ != nullptr;
    }

    /**
     * @brief The descriptor itself.
     * @return The self-relative descriptor.
     */
    PSECURITY_DESCRIPTOR Descriptor() const
    {
        return descriptor_;
    }

    /**
     * @brief Describe the descriptor as SECURITY_ATTRIBUTES.
     *
     * The attributes carry the DACL of the descriptor only; the mandatory label
     * is set on the instance separately.
     *
     * @return The attributes, valid while the descriptor lives.
     */
    SECURITY_ATTRIBUTES* Attributes()
    {
        attributes_.nLength = sizeof(attributes_);
        attributes_.lpSecurityDescriptor = descriptor_;
        attributes_.bInheritHandle = FALSE;
        return &attributes_;
    }

private:
    /**
     * @brief Release the descriptor.
     */
    void Reset()
    {
        if (descriptor_ != nullptr)
        {
            ::LocalFree(descriptor_);
            descriptor_ = nullptr;
        }
    }

    PSECURITY_DESCRIPTOR descriptor_ = nullptr; /* Self-relative descriptor */
    SECURITY_ATTRIBUTES  attributes_ = {};      /* View of the descriptor */
};

} // namespace detail

/**
 * @brief Internal state of one RemoteAccepter.
 */
struct RemoteAccepter::Data : std::enable_shared_from_this<Data>
{
    /**
     * @brief Lifecycle of the accepter.
     */
    enum class Lifecycle
    {
        Running,  /* Accepting connections */
        Stopping, /* Refusing connections, waiting for the pending work */
        Stopped,  /* The IO thread ended */
    };

    typedef std::map<std::uint64_t, RemoteSession::Ptr> SessionMap;

    Data() : io_context(std::make_shared<asio::io_context>()), uid_gen(0)
    {
    }

    ~Data()
    {
        Stop();
    }

    /**
     * @brief Create one instance of the pipe.
     * @param[out] error Error code of a failed creation.
     * @return The handle of the instance, INVALID_HANDLE_VALUE on failure.
     */
    HANDLE CreateInstance(DWORD& error)
    {
        DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;

        if (first_instance)
        {
            /*
             * The flag tells this process whether it won the race for the name,
             * it is only valid for the very first instance of the pipe.
             */
            open_mode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
        }
        first_instance = false;

        HANDLE h = CreatePipeInstance(open_mode, security.Valid() ? security.Attributes() : nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            error = ::GetLastError();

            /*
             * Some environments refuse the mandatory label of the descriptor.
             * The DACL alone still bounds the access to the user, so the
             * instance is created with it instead of failing the server.
             */
            if (security.Valid() && security_dacl_only.Valid() &&
                (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD))
            {
                h = CreatePipeInstance(open_mode, security_dacl_only.Attributes());
                if (h != INVALID_HANDLE_VALUE)
                {
                    SPDLOG_WARN("the integrity label of the pipe is refused, the pipe is created without it");
                    error = ERROR_SUCCESS;
                    return h;
                }

                error = ::GetLastError();
            }

            return h;
        }

        error = ERROR_SUCCESS;
        return h;
    }

    /**
     * @brief Create one instance with the given access mode and security.
     * @param[in] open_mode Access mode of the instance.
     * @param[in] attributes Security of the instance, null for the default.
     * @return The handle of the instance, INVALID_HANDLE_VALUE on failure.
     */
    HANDLE CreatePipeInstance(DWORD open_mode, SECURITY_ATTRIBUTES* attributes)
    {
        return ::CreateNamedPipeA(pipe_path.c_str(), open_mode, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                  PIPE_UNLIMITED_INSTANCES, 4096, /* Output buffer */
                                  4096,                           /* Input buffer */
                                  0,                              /* Default timeout */
                                  attributes);
    }

    /**
     * @brief Create one instance of the pipe and wait for a client to connect.
     * @return true when an instance waits for a client, otherwise false.
     */
    bool AcceptNext()
    {
        if (stopping)
        {
            return false;
        }

        if (options.create_instance_error)
        {
            const DWORD injected = options.create_instance_error();
            if (injected != ERROR_SUCCESS)
            {
                ReportAcceptFailure(injected);
                return false;
            }
        }

        DWORD        error = ERROR_SUCCESS;
        const HANDLE handle = CreateInstance(error);
        if (handle == INVALID_HANDLE_VALUE)
        {
            ReportAcceptFailure(error);
            return false;
        }

        std::shared_ptr<asio::windows::stream_handle> pipe;
        try
        {
            pipe = std::make_shared<asio::windows::stream_handle>(*io_context, handle);
        }
        catch (const std::exception& e)
        {
            ::CloseHandle(handle);
            SPDLOG_ERROR("cannot wrap the pipe instance: {}", e.what());
            ReportAcceptFailure(ERROR_INVALID_HANDLE);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(listener_mutex);
            if (stopping)
            {
                /* The handle is closed when the instance is released. */
                return false;
            }
            listener = pipe;
        }

        BeginAccept();
        return true;
    }

    /**
     * @brief Start waiting for a client on the instance which is held.
     */
    void BeginAccept()
    {
        std::shared_ptr<asio::windows::stream_handle> pipe;
        {
            std::lock_guard<std::mutex> lock(listener_mutex);
            if (stopping)
            {
                return;
            }
            pipe = listener;
        }

        if (!pipe)
        {
            return;
        }

        auto                          self = shared_from_this();
        asio::windows::overlapped_ptr ov(
            *io_context, [self, pipe](asio::error_code ec, std::size_t) { self->OnConnected(ec, pipe); });

        const BOOL  ok = ::ConnectNamedPipe(pipe->native_handle(), ov.get());
        const DWORD err = ::GetLastError();

        if (ok)
        { /* Finished synchronously */
            ov.complete(asio::error_code(), 0);
            return;
        }

        if (err == ERROR_IO_PENDING)
        { /* Completion is delivered by the IOCP */
            ov.release();
            return;
        }

        if (err == ERROR_PIPE_CONNECTED)
        { /* Client connected between CreateNamedPipe() and ConnectNamedPipe() */
            ov.complete(asio::error_code(), 0);
            return;
        }

        /* Error occurred */
        ov.complete(asio::error_code(err, std::system_category()), 0);
    }

    /**
     * @brief Handle one finished ConnectNamedPipe() attempt.
     * @param[in] ec Error code of the connect.
     * @param[in] pipe The connected pipe handle.
     */
    void OnConnected(asio::error_code ec, std::shared_ptr<asio::windows::stream_handle> pipe)
    {
        if (ec)
        {
            if (stopping)
            {
                /* The accept was cancelled by Stop(). */
                return;
            }

            const DWORD error = static_cast<DWORD>(ec.value());
            if (detail::AccepterConnectionWasAbandoned(error))
            {
                /*
                 * A client which connects and leaves before the connection was
                 * established consumes the instance without starting a session.
                 * The server still serves, so the next instance is created
                 * instead of reporting a failure: a launcher which probes the
                 * pipe of its sandbox must not stop the sandbox it probes.
                 */
                SPDLOG_DEBUG("[HANDLE: {}] a client left the pipe before the connection was established: {}",
                             static_cast<void*>(pipe->native_handle()), ec.message());
                AcceptNext();
                return;
            }

            SPDLOG_WARN("[HANDLE: {}] server accept failed: {}", static_cast<void*>(pipe->native_handle()),
                        ec.message());
            ReportAcceptFailure(error);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(listener_mutex);
            listener.reset();
        }

        DWORD peer_pid = 0;
        if (!::GetNamedPipeClientProcessId(pipe->native_handle(), &peer_pid))
        {
            SPDLOG_WARN("[HANDLE: {}] cannot query the client process: {}", static_cast<void*>(pipe->native_handle()),
                        ::GetLastError());
            peer_pid = 0;
        }

        auto session = RemoteSession::Create(pipe, peer_pid, io_context);
        session->SetMaxRequestSize(options.max_request_size);

        /*
         * The methods of the session are registered before it reads its first
         * message, so a peer which sends a request at once finds its handler.
         */
        SessionCallback callback;
        {
            std::lock_guard<std::mutex> lock(callback_mutex);
            callback = session_callback;
        }

        if (callback)
        {
            try
            {
                callback(session);
            }
            catch (const std::exception& e)
            {
                SPDLOG_ERROR("the session callback failed: {}", e.what());
            }
            catch (...)
            {
                SPDLOG_ERROR("the session callback failed");
            }
        }

        /*
         * The accepter owns the session, so it keeps the pipe of the connection
         * open while the peer uses it. The session reports the end of its
         * connection, which is what releases the entry again; the handler holds
         * the accepter weakly, because a strong reference would keep both alive
         * for ever.
         */
        const std::uint64_t session_id = uid_gen++;
        std::weak_ptr<Data> weak_self = shared_from_this();
        session->SetClosedHandler([weak_self, session_id]() {
            if (auto self = weak_self.lock())
            {
                self->OnSessionClosed(session_id);
            }
        });

        {
            std::lock_guard<std::mutex> lock(session_map_mutex);
            session_map[session_id] = session;
        }

        session->Start();

        /* Keep at least one instance of the pipe alive for the next client. */
        AcceptNext();
    }

    /**
     * @brief Release one session whose connection ended.
     *
     * The session reports its own end, so the table never holds a connection
     * which serves nothing: the accepter is the owner of every session it
     * accepted, which is what keeps the pipe of the connection open while the
     * peer uses it.
     *
     * @param[in] session_id Id of the session which ended.
     */
    void OnSessionClosed(std::uint64_t session_id)
    {
        std::lock_guard<std::mutex> lock(session_map_mutex);
        session_map.erase(session_id);
    }

    /**
     * @brief Collect the sessions which are still connected.
     * @return The sessions, in the order they were accepted in.
     */
    std::vector<RemoteSession::Ptr> LiveSessions()
    {
        std::vector<RemoteSession::Ptr> sessions;

        std::lock_guard<std::mutex> lock(session_map_mutex);
        for (const auto& item : session_map)
        {
            sessions.push_back(item.second);
        }

        return sessions;
    }

    /**
     * @brief Report a failure of the accept loop.
     *
     * A transient failure is retried with a growing delay, a fatal one is
     * handed to the callback of the control plane. Nothing is thrown, the
     * failure is reported on the IO thread which must not be left by an
     * exception.
     *
     * @param[in] error Error code of the failed instance creation.
     */
    void ReportAcceptFailure(DWORD error)
    {
        const AcceptFailureKind kind = detail::ClassifyAccepterAcceptError(error);

        if (kind == AcceptFailureKind::StillServing && accept_retry < kAcceptRetryDelaysMs.size() && !stopping)
        {
            const std::uint32_t delay_ms = kAcceptRetryDelaysMs[accept_retry++];

            SPDLOG_WARN("cannot create a pipe instance: {}, retry {} in {} ms", error, accept_retry, delay_ms);

            auto self = shared_from_this();
            auto timer = std::make_shared<asio::steady_timer>(*io_context, std::chrono::milliseconds(delay_ms));
            timer->async_wait([self, timer](const asio::error_code& ec) {
                if (ec)
                { /* Cancelled by the shutdown. */
                    return;
                }
                if (self->stopping)
                {
                    return;
                }
                self->AcceptNext();
            });
            return;
        }

        SPDLOG_ERROR("the pipe server cannot accept connections any more: {}", error);
        accept_failed = true;

        {
            std::lock_guard<std::mutex> lock(listener_mutex);
            listener.reset();
        }

        AcceptFailureCallback callback;
        {
            std::lock_guard<std::mutex> lock(callback_mutex);
            callback = accept_failure_callback;
        }

        if (!callback)
        {
            return;
        }

        try
        {
            callback(kind, error);
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("the accept failure callback failed: {}", e.what());
        }
        catch (...)
        {
            SPDLOG_ERROR("the accept failure callback failed");
        }
    }

    /**
     * @brief Start the IO thread and the accept loop.
     * @return true when the accepter is listening, otherwise false.
     */
    bool Start()
    {
        std::lock_guard<std::mutex> lock(state_mutex);

        if (state != Lifecycle::Running)
        {
            SPDLOG_ERROR("the pipe server cannot be started, it is stopping");
            return false;
        }

        if (thread.joinable())
        {
            SPDLOG_ERROR("the pipe server is already started");
            return false;
        }

        auto self = shared_from_this();
        auto context = io_context;

        asio::post(*context, [self]() { self->BeginAccept(); });

        /*
         * The thread does not log when it ends: the accepter may be destroyed by
         * a static destructor of the process, which runs after the logger of
         * the process is gone.
         */
        thread = std::thread([context]() { context->run(); });

        return true;
    }

    /**
     * @brief Stop accepting connections and end the IO thread.
     *
     * The call is idempotent. The requests which are already handled are
     * answered first, then the sessions are drained and released, and the IO
     * thread ends.
     */
    void Stop()
    {
        {
            std::unique_lock<std::mutex> lock(state_mutex);
            if (state == Lifecycle::Stopped)
            {
                return;
            }

            if (state == Lifecycle::Stopping)
            {
                /* Another thread is stopping, wait until it is done. */
                state_cv.wait(lock, [this] { return state == Lifecycle::Stopped; });
                return;
            }

            state = Lifecycle::Stopping;
        }

        stopping = true;

        /*
         * Refuse new connections: the pending accept is cancelled and the
         * instance is released, which closes the handle on the IO thread when
         * the completion handler drops its reference.
         */
        std::shared_ptr<asio::windows::stream_handle> listener_to_close;
        {
            std::lock_guard<std::mutex> lock(listener_mutex);
            listener_to_close.swap(listener);
        }
        if (listener_to_close)
        {
            ::CancelIoEx(listener_to_close->native_handle(), nullptr);
        }
        listener_to_close.reset();

        /*
         * Wait for the requests which the sessions are already handling. The
         * budget belongs to the whole stop, so a session which is idle costs
         * nothing and the stop is bounded whatever the number of sessions is.
         */
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kInflightTimeoutMs);
        std::vector<RemoteSession::Ptr> sessions = LiveSessions();
        for (const auto& session : sessions)
        {
            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left.count() <= 0)
            {
                SPDLOG_WARN("the requests of the sessions are still unanswered, the sessions are drained");
                break;
            }

            session->WaitForIncomingResponses(static_cast<std::uint32_t>(left.count()));
        }

        /* Let the answers reach the peers, then release the sessions. */
        for (const auto& session : sessions)
        {
            session->WaitForPendingWrites(kSessionDrainTimeoutMs);
            session->Close();
        }
        sessions.clear();

        /*
         * The table is released outside of its lock: a session reports the end of
         * its connection when it is destroyed, and that report takes the lock.
         */
        SessionMap remaining;
        {
            std::lock_guard<std::mutex> lock(session_map_mutex);
            remaining.swap(session_map);
        }
        remaining.clear();

        /* Stop the IO thread and release the handlers which are still queued. */
        if (io_context)
        {
            io_context->stop();
        }

        if (thread.joinable())
        {
            if (std::this_thread::get_id() == thread.get_id())
            {
                SPDLOG_WARN("the pipe server is stopped from its own IO thread, the thread is detached");
                thread.detach();
            }
            else
            {
                thread.join();
            }
        }

        io_context.reset();

        {
            std::lock_guard<std::mutex> lock(state_mutex);
            state = Lifecycle::Stopped;
        }
        state_cv.notify_all();
    }

    /* Delays of the retries of a transient accept failure, in milliseconds. */
    static constexpr std::array<std::uint32_t, 3> kAcceptRetryDelaysMs = { 50, 100, 200 };

    /* Upper bound of the wait for the requests of the sessions of a stop, in milliseconds. */
    static constexpr std::uint32_t kInflightTimeoutMs = 5000;

    /* Upper bound of the wait for the answers of one session, in milliseconds. */
    static constexpr std::uint32_t kSessionDrainTimeoutMs = 2000;

    std::string pipe_path; /* Name of the pipe */
    Options     options;   /* Options of the accepter */

    detail::RemoteAccepterSecurityDescriptor security;           /* DACL and label of the pipe */
    detail::RemoteAccepterSecurityDescriptor security_dacl_only; /* DACL of the pipe without the label */

    std::shared_ptr<asio::io_context> io_context; /* IO context of the private IO thread */

    std::thread          thread;  /* Working thread */
    std::atomic_uint64_t uid_gen; /* Session id generator */

    std::shared_ptr<asio::windows::stream_handle> listener;               /* Instance which waits for a client */
    bool                                          first_instance = false; /* First creation of the pipe */
    std::mutex                                    listener_mutex;         /* Listener lock */

    SessionMap         session_map;       /* Sessions which are still connected */
    mutable std::mutex session_map_mutex; /* Session map lock */

    SessionCallback session_callback; /* Callback which registers the methods of a session */

    AcceptFailureCallback accept_failure_callback; /* Fatal accept failure callback */
    std::mutex            callback_mutex;          /* Callback lock */

    Lifecycle               state = Lifecycle::Running; /* Lifecycle of the accepter */
    mutable std::mutex      state_mutex;                /* State lock */
    std::condition_variable state_cv;                   /* Signals a state change */
    std::size_t             accept_retry = 0;           /* Retries of the accept loop */
    std::atomic_bool        stopping = false;           /* Stop was requested */
    std::atomic_bool        accept_failed = false;      /* The accept loop is dead */
};

/* Definition of the contract on the declaration. */
inline RemoteAccepter::RemoteAccepter() : data_(std::make_shared<Data>())
{
}

/* Definition of the contract on the declaration. */
inline RemoteAccepter::~RemoteAccepter()
{
    if (data_)
    {
        data_->Stop();
    }
    data_.reset();
}

/* Definition of the contract on the declaration. */
inline RemoteAccepter::Ptr RemoteAccepter::Create(const std::string& pipe)
{
    Options options;
    options.first_instance = false;

    CreateResult result = CreateCompetitive(pipe, options);
    if (result.status != ServerCreateStatus::Created || !result.accepter)
    {
        throw std::system_error(static_cast<int>(result.error_code), std::system_category(),
                                "CreateNamedPipe() failed");
    }

    return result.accepter;
}

/* Definition of the contract on the declaration. */
inline RemoteAccepter::CreateResult RemoteAccepter::CreateCompetitive(const std::string& pipe, const Options& options)
{
    CreateResult result;

    Ptr obj(new RemoteAccepter);
    obj->data_->pipe_path = pipe;
    obj->data_->options = options;
    obj->data_->first_instance = options.first_instance;

    if (options.restrict_to_current_user && (!obj->data_->security.Build(detail::kRemoteAccepterPipeSddl) ||
                                             !obj->data_->security_dacl_only.Build(detail::kRemoteAccepterPipeDacl)))
    {
        const DWORD error = ::GetLastError();
        SPDLOG_ERROR("cannot build the security descriptor of the pipe: {}", error);

        result.status = ServerCreateStatus::Failed;
        result.error_code = error;
        return result;
    }

    DWORD        error = ERROR_SUCCESS;
    const HANDLE handle = obj->data_->CreateInstance(error);
    if (handle == INVALID_HANDLE_VALUE)
    {
        const bool peer_reachable = (error == ERROR_ACCESS_DENIED) && detail::AccepterPeerIsReachable(pipe);

        result.status = detail::ClassifyAccepterCreateError(error, peer_reachable);
        result.error_code = error;

        SPDLOG_DEBUG("cannot create the pipe '{}': {} ({})", pipe, error, static_cast<int>(result.status));
        return result;
    }

    try
    {
        obj->data_->listener = std::make_shared<asio::windows::stream_handle>(*obj->data_->io_context, handle);
    }
    catch (const std::exception& e)
    {
        ::CloseHandle(handle);
        SPDLOG_ERROR("cannot wrap the pipe instance: {}", e.what());

        result.status = ServerCreateStatus::Failed;
        result.error_code = ERROR_INVALID_HANDLE;
        return result;
    }

    result.status = ServerCreateStatus::Created;
    result.error_code = ERROR_SUCCESS;
    result.accepter = obj;
    return result;
}

/* Definition of the contract on the declaration. */
inline void RemoteAccepter::SetSessionCallback(SessionCallback callback)
{
    std::lock_guard<std::mutex> lock(data_->callback_mutex);
    data_->session_callback = std::move(callback);
}

/* Definition of the contract on the declaration. */
inline bool RemoteAccepter::Start()
{
    return data_->Start();
}

/* Definition of the contract on the declaration. */
inline void RemoteAccepter::Stop()
{
    if (data_)
    {
        data_->Stop();
    }
}

/* Definition of the contract on the declaration. */
inline void RemoteAccepter::SetAcceptFailureCallback(AcceptFailureCallback callback)
{
    std::lock_guard<std::mutex> lock(data_->callback_mutex);
    data_->accept_failure_callback = std::move(callback);
}

} // namespace appbox

#endif // APPBOX_COMMON_DETAILS_REMOTE_ACCEPTER_DETAIL_HPP
