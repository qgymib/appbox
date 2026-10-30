#ifndef APPBOX_COMMON_DETAILS_REMOTE_ACCEPTER_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_REMOTE_ACCEPTER_DETAIL_HPP

/*
 * Implementation of `common/RemoteAccepter.hpp`: the internal state of the
 * accepter, the pipe handling on its private thread and the members of the
 * class. The header is included by the public header and is not meant to be
 * included on its own.
 */

#include "RemoteAccepter.hpp"
#include "RemoteThread.hpp"
#include <aclapi.h>
#include <sddl.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
        Stopped,  /* The accept thread ended */
    };

    typedef std::map<std::uint64_t, RemoteSession::Ptr> SessionMap;

    /**
     * @brief Create the events the accept thread waits on.
     *
     * The events are automatic reset events: a wait consumes the signal, so the
     * thread which sets one never has to know whether the accept thread already
     * noticed the previous one.
     */
    Data()
    {
        accept_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        stop_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }

    ~Data()
    {
        Stop();

        /* The accept thread ended above, so nothing uses the events any more. */
        detail::CloseHandleIfValid(accept_event);
        detail::CloseHandleIfValid(stop_event);
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
     * @brief Take the instance which waits for the first client.
     *
     * The instance of the race for the name is created by CreateCompetitive()
     * and handed to the accept thread through this member, which is why the
     * member is only a hand-off slot: the thread owns the handle from the moment
     * it took it.
     *
     * @return The handle, `INVALID_HANDLE_VALUE` when the slot is empty.
     */
    HANDLE TakeListener()
    {
        std::lock_guard<std::mutex> lock(listener_mutex);

        const HANDLE handle = listener;
        listener = INVALID_HANDLE_VALUE;
        return handle;
    }

    /**
     * @brief Create the instance which waits for the next client.
     *
     * The options of the accepter may inject an error instead of creating the
     * instance, which is what the cases of the retries of the accept loop use.
     *
     * @param[out] error Error code of a failed creation.
     * @return The handle of the instance, INVALID_HANDLE_VALUE on failure.
     */
    HANDLE CreateNextInstance(DWORD& error)
    {
        if (options.create_instance_error)
        {
            const DWORD injected = options.create_instance_error();
            if (injected != ERROR_SUCCESS)
            {
                error = injected;
                return INVALID_HANDLE_VALUE;
            }
        }

        return CreateInstance(error);
    }

    /**
     * @brief Wait until a client connected to the given instance.
     *
     * The connect is overlapped, so the wait of the thread is bounded by the
     * stop event: Stop() interrupts it, and the cancellation is waited for
     * before the handle is released, so the overlapped state of the connect is
     * never reused while the system still writes it.
     *
     * @param[in] instance Instance of the pipe to wait on.
     * @param[out] error Error code of a failed connect.
     * @return true when a client connected, otherwise false.
     */
    bool WaitForClient(HANDLE instance, DWORD& error)
    {
        std::memset(&accept_ov, 0, sizeof(accept_ov));
        accept_ov.hEvent = accept_event;

        if (::ConnectNamedPipe(instance, &accept_ov))
        { /* Finished synchronously */
            return true;
        }

        const DWORD pending_error = ::GetLastError();
        if (pending_error == ERROR_PIPE_CONNECTED)
        { /* Client connected between CreateNamedPipe() and ConnectNamedPipe() */
            return true;
        }

        if (pending_error != ERROR_IO_PENDING)
        {
            error = pending_error;
            return false;
        }

        const HANDLE handles[2] = { accept_event, stop_event };
        const DWORD  index = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);

        if (index == detail::kWaitObjectFirst)
        {
            DWORD transferred = 0;
            if (::GetOverlappedResult(instance, &accept_ov, &transferred, FALSE))
            {
                return true;
            }

            error = ::GetLastError();
            return false;
        }

        if (index == detail::kWaitObjectFirst + 1)
        { /* The accept was interrupted by Stop(). */
            ::CancelIoEx(instance, &accept_ov);
            ::WaitForSingleObject(accept_event, INFINITE);
            error = ERROR_OPERATION_ABORTED;
            return false;
        }

        error = ::GetLastError();
        return false;
    }

    /**
     * @brief Hand one connected instance over to a session of its own.
     *
     * The session owns the handle from here on, so the connection lives exactly
     * as long as the session does.
     *
     * @param[in] instance Connected instance of the pipe.
     */
    void OnConnected(HANDLE instance)
    {
        DWORD peer_pid = 0;
        if (!::GetNamedPipeClientProcessId(instance, &peer_pid))
        {
            SPDLOG_WARN("[HANDLE: {}] cannot query the client process: {}", static_cast<void*>(instance),
                        ::GetLastError());
            peer_pid = 0;
        }

        RemoteSession::Ptr session;
        try
        {
            session = RemoteSession::Create(instance, peer_pid);
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("cannot create the session of the connection: {}", e.what());
            ::CloseHandle(instance);
            return;
        }

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
     * @brief Release every session which is still in the table.
     */
    void ReleaseSessions()
    {
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
    }

    /**
     * @brief Report a failure of the accept loop and decide whether it goes on.
     *
     * A transient failure is retried with a growing delay, a fatal one is handed
     * to the callback of the control plane. Nothing is thrown, the failure is
     * reported on the accept thread which must not be left by an exception.
     *
     * @param[in] error Error code of the failed instance creation.
     * @return true when the loop retries, false when it ends.
     */
    bool HandleAcceptFailure(DWORD error)
    {
        const AcceptFailureKind kind = detail::ClassifyAccepterAcceptError(error);

        if (kind == AcceptFailureKind::StillServing && accept_retry < kAcceptRetryDelaysMs.size() && !stopping.load())
        {
            const std::uint32_t delay_ms = kAcceptRetryDelaysMs[accept_retry++];

            SPDLOG_WARN("cannot create a pipe instance: {}, retry {} in {} ms", error, accept_retry, delay_ms);

            /* The wait is bounded by the stop event, so a stop does not wait for it. */
            if (::WaitForSingleObject(stop_event, delay_ms) == detail::kWaitObjectFirst)
            {
                return false;
            }

            return !stopping.load();
        }

        SPDLOG_ERROR("the pipe server cannot accept connections any more: {}", error);
        accept_failed = true;

        HANDLE listener_to_close = TakeListener();
        detail::CloseHandleIfValid(listener_to_close);

        AcceptFailureCallback callback;
        {
            std::lock_guard<std::mutex> lock(callback_mutex);
            callback = accept_failure_callback;
        }

        if (!callback)
        {
            return false;
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

        return false;
    }

    /**
     * @brief The loop of the accept thread.
     *
     * The loop keeps at least one instance of the pipe open for its whole life:
     * it creates an instance, waits for a client, hands the connected instance
     * to a session of its own and creates the next instance at once, so the name
     * of the sandbox never disappears while the server serves.
     */
    void Run()
    {
        for (;;)
        {
            if (stopping.load())
            {
                return;
            }

            HANDLE instance = TakeListener();
            if (instance == INVALID_HANDLE_VALUE)
            {
                DWORD error = ERROR_SUCCESS;
                instance = CreateNextInstance(error);
                if (instance == INVALID_HANDLE_VALUE)
                {
                    if (!HandleAcceptFailure(error))
                    {
                        return;
                    }
                    continue;
                }
            }

            if (stopping.load())
            {
                detail::CloseHandleIfValid(instance);
                return;
            }

            DWORD error = ERROR_SUCCESS;
            if (!WaitForClient(instance, error))
            {
                detail::CloseHandleIfValid(instance);

                if (stopping.load())
                {
                    return;
                }

                if (detail::AccepterConnectionWasAbandoned(error))
                {
                    /*
                     * A client which connects and leaves before the connection
                     * was established consumes the instance without starting a
                     * session. The server still serves, so the next instance is
                     * created instead of reporting a failure: a launcher which
                     * probes the pipe of its sandbox must not stop the sandbox
                     * it probes.
                     */
                    SPDLOG_DEBUG("a client left the pipe before the connection was established: {}", error);
                    continue;
                }

                if (!HandleAcceptFailure(error))
                {
                    return;
                }
                continue;
            }

            if (stopping.load())
            {
                detail::CloseHandleIfValid(instance);
                return;
            }

            OnConnected(instance);
        }
    }

    /**
     * @brief Start the accept thread.
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

        if (accept_event == nullptr || stop_event == nullptr)
        {
            SPDLOG_ERROR("the events of the pipe server cannot be created");
            return false;
        }

        try
        {
            /*
             * The thread holds the state of the accepter, so a server which the
             * last caller releases while its thread still runs is not destroyed
             * under that thread.
             */
            auto self = shared_from_this();
            thread = std::thread([self]() { self->Run(); });
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("the accept thread of the pipe server cannot be created: {}", e.what());
            return false;
        }

        return true;
    }

    /**
     * @brief Stop accepting connections and end the accept thread.
     *
     * The call is idempotent. The requests which are already handled are
     * answered first, then the sessions are drained and released, and the accept
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
         * Refuse new connections: the instance which waits for a client is
         * released, the pending connect is cancelled and the accept thread is
         * woken up, so it leaves its loop instead of waiting for a client which
         * never comes.
         */
        HANDLE listener_to_close = TakeListener();
        if (listener_to_close != INVALID_HANDLE_VALUE)
        {
            ::CancelIoEx(listener_to_close, nullptr);
        }

        if (stop_event != nullptr)
        {
            ::SetEvent(stop_event);
        }

        detail::CloseHandleIfValid(listener_to_close);

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

        ReleaseSessions();

        /* End the accept thread and release the handlers which are still queued. */
        detail::JoinOrDetach(thread);

        /* A connection which completed while the stop ran is released as well. */
        ReleaseSessions();

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

    HANDLE     accept_event = nullptr; /* Signalled when the pending connect completed */
    HANDLE     stop_event = nullptr;   /* Set by Stop() to interrupt the accept thread */
    OVERLAPPED accept_ov = {};         /* Overlapped state of the connect in flight */

    std::thread          thread;       /* Accept thread */
    std::atomic_uint64_t uid_gen{ 0 }; /* Session id generator */

    HANDLE     listener = INVALID_HANDLE_VALUE; /* Instance which waits for a client */
    bool       first_instance = false;          /* First creation of the pipe */
    std::mutex listener_mutex;                  /* Listener lock */

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

    /*
     * The first instance waits for the first client on the accept thread, which
     * takes it out of this slot when it starts.
     */
    {
        std::lock_guard<std::mutex> lock(obj->data_->listener_mutex);
        obj->data_->listener = handle;
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
