#ifndef APPBOX_COMMON_DETAILS_REMOTE_SERVER_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_REMOTE_SERVER_DETAIL_HPP

/*
 * Implementation of `common/RemoteServer.hpp`: the internal state of the server,
 * the pipe handling on the private IO thread and the members of the class. The
 * header is included by the public header and is not meant to be included on its
 * own.
 */

#include "RemoteServer.hpp"
#include <aclapi.h>
#include <sddl.h>
#include <asio.hpp>
#include <spdlog/spdlog.h>
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
constexpr const char* kRemoteServerPipeDacl = "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)";

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
constexpr const char* kRemoteServerPipeSddl = "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)S:(ML;;NW;;;ME)";

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
inline ServerCreateStatus ClassifyRemoteServerCreateError(DWORD error, bool peer_reachable)
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
inline AcceptFailureKind ClassifyRemoteServerAcceptError(DWORD error)
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
inline bool RemoteServerConnectionWasAbandoned(DWORD error)
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
inline bool RemoteServerPeerIsReachable(const std::string& pipe)
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
class RemoteServerSecurityDescriptor
{
public:
    RemoteServerSecurityDescriptor() = default;

    ~RemoteServerSecurityDescriptor()
    {
        Reset();
    }

    RemoteServerSecurityDescriptor(const RemoteServerSecurityDescriptor&) = delete;
    RemoteServerSecurityDescriptor& operator=(const RemoteServerSecurityDescriptor&) = delete;

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
 * @brief Internal state of one RemoteServer.
 */
struct RemoteServer::Data : std::enable_shared_from_this<Data>
{
    /**
     * @brief One request received from a client, kept until its response is sent.
     */
    struct IncomingRequest
    {
        typedef std::shared_ptr<IncomingRequest> Ptr;

        IncomingRequest(uint64_t id, uint64_t session_id) : uid(id), session_id(session_id)
        {
        }

        const uint64_t uid;        /* Global unique id */
        const uint64_t session_id; /* Session id */
        nlohmann::json request;    /* Client request */
        std::string    method;     /* Request method */
        nlohmann::json id;         /* Request id in the request object */
    };

    /**
     * @brief One connected client.
     */
    struct SessionEntry
    {
        RemoteSession::Ptr session;  /* Session of the connection */
        DWORD              peer_pid; /* Process at the other end, zero when unknown */

        SessionEntry() : peer_pid(0)
        {
        }

        SessionEntry(RemoteSession::Ptr s, DWORD pid) : session(std::move(s)), peer_pid(pid)
        {
        }
    };

    /**
     * @brief Lifecycle of the server.
     */
    enum class Lifecycle
    {
        Running,  /* Accepting connections */
        Stopping, /* Refusing connections, waiting for the pending work */
        Stopped,  /* The IO thread ended */
    };

    typedef std::map<std::string, MethodCallback>    MethodCallbackMap;
    typedef std::map<uint64_t, SessionEntry>         SessionMap;
    typedef std::map<uint64_t, IncomingRequest::Ptr> IncomingRequestMap;

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
            if (detail::RemoteServerConnectionWasAbandoned(error))
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

        const uint64_t session_id = uid_gen++;

        DWORD peer_pid = 0;
        if (!::GetNamedPipeClientProcessId(pipe->native_handle(), &peer_pid))
        {
            SPDLOG_WARN("[HANDLE: {}] cannot query the client process: {}", static_cast<void*>(pipe->native_handle()),
                        ::GetLastError());
            peer_pid = 0;
        }

        auto self = shared_from_this();
        auto session =
            RemoteSession::Create(pipe, [self, session_id](const asio::error_code& ec, RemoteSession::MsgPtr data) {
                if (ec)
                {
                    std::lock_guard<std::mutex> guard(self->session_map_mutex);
                    self->session_map.erase(session_id);
                    return;
                }
                self->OnDataReceived(session_id, std::move(data));
            });

        /* Save session */
        {
            std::lock_guard<std::mutex> lock(session_map_mutex);
            session_map[session_id] = SessionEntry(session, peer_pid);
        }

        session->Start();

        /* Keep at least one instance of the pipe alive for the next client. */
        AcceptNext();
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
        const AcceptFailureKind kind = detail::ClassifyRemoteServerAcceptError(error);

        if (kind == AcceptFailureKind::StillServing && accept_retry < kAcceptRetryDelaysMs.size() && !stopping)
        {
            const uint32_t delay_ms = kAcceptRetryDelaysMs[accept_retry++];

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
     * @brief Parse one received message and dispatch it to the registered callback.
     *
     * Every request is answered, also when it is malformed: the failure is
     * reported as a JSON-RPC error instead of ending the IO thread.
     *
     * @param[in] session_id Id of the session which received the message.
     * @param[in] data Message payload.
     */
    void OnDataReceived(uint64_t session_id, RemoteSession::MsgPtr data)
    {
        if (!data)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex);
            ++inflight;
        }

        auto req = std::make_shared<IncomingRequest>(uid_gen++, session_id);

        if (data->size() > options.max_request_size)
        {
            SPDLOG_WARN("request of {} bytes exceeds the limit of {} bytes", data->size(), options.max_request_size);
            RegisterRequest(req);
            SendError(req->uid, -32600, "Invalid Request");
            return;
        }

        try
        {
            req->request = nlohmann::json::parse(*data);
        }
        catch (const std::exception& e)
        {
            SPDLOG_WARN("malformed request: {}", e.what());
            RegisterRequest(req);
            SendError(req->uid, -32700, "Parse error");
            return;
        }

        if (!req->request.is_object())
        {
            SPDLOG_WARN("request is not an object");
            RegisterRequest(req);
            SendError(req->uid, -32600, "Invalid Request");
            return;
        }

        const auto it_id = req->request.find("id");
        if (it_id != req->request.end())
        {
            req->id = *it_id;
        }

        const auto it_method = req->request.find("method");
        if (it_method == req->request.end() || !it_method->is_string() || it_id == req->request.end())
        {
            SPDLOG_WARN("request has no method or no id");
            RegisterRequest(req);
            SendError(req->uid, -32600, "Invalid Request");
            return;
        }

        req->method = it_method->get<std::string>();

        /* Save to request map */
        RegisterRequest(req);

        MethodCallback fn;
        {
            std::lock_guard<std::mutex> lock(method_map_mutex);
            const auto                  it = method_callbacks.find(req->method);
            if (it == method_callbacks.end())
            {
                SPDLOG_ERROR("method not found: {}", req->method);
                SendError(req->uid, -32601, "Method not found");
                return;
            }
            fn = it->second;
        }

        const auto           it_params = req->request.find("params");
        const nlohmann::json param = (it_params == req->request.end()) ? nlohmann::json(nullptr) : *it_params;

        try
        {
            fn(req->uid, param);
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("the handler of {} failed: {}", req->method, e.what());
            SendError(req->uid, -32603, "Internal error");
        }
        catch (...)
        {
            SPDLOG_ERROR("the handler of {} failed", req->method);
            SendError(req->uid, -32603, "Internal error");
        }
    }

    /**
     * @brief Keep one request until its response is sent.
     * @param[in] req Request to keep.
     */
    void RegisterRequest(const IncomingRequest::Ptr& req)
    {
        std::lock_guard<std::mutex> lock(request_map_mutex);
        request_map.insert(IncomingRequestMap::value_type(req->uid, req));
    }

    /**
     * @brief Post one handler to the IO thread.
     * @param[in] fn Handler to run on the IO thread.
     */
    void PostToIo(std::function<void()> fn)
    {
        std::lock_guard<std::mutex> lock(io_post_mutex);
        if (!io_context)
        {
            SPDLOG_WARN("the pipe server is stopped, the message is dropped");
            return;
        }

        asio::post(*io_context, std::move(fn));
    }

    /**
     * @brief Send a success response for the given request.
     * @param[in] id Request id.
     * @param[in] result Result value.
     */
    void SendResponse(uint64_t id, const nlohmann::json& result)
    {
        auto rsp = std::make_shared<nlohmann::json>();
        (*rsp)["jsonrpc"] = "2.0";
        (*rsp)["result"] = result;

        auto self = shared_from_this();
        PostToIo([self, id, rsp]() { self->SendResponseMsg(id, rsp); });
    }

    /**
     * @brief Send an error response for the given request.
     * @param[in] id Request id.
     * @param[in] errcode Error code.
     * @param[in] err_msg Error message.
     * @param[in] err_data Additional error data.
     */
    void SendError(uint64_t id, int errcode, const std::string& err_msg, const nlohmann::json& err_data = nullptr)
    {
        nlohmann::json err;
        err["code"] = errcode;
        err["message"] = err_msg;
        if (!err_data.is_null())
        {
            err["data"] = err_data;
        }

        auto rsp = std::make_shared<nlohmann::json>();
        (*rsp)["jsonrpc"] = "2.0";
        (*rsp)["error"] = err;

        auto self = shared_from_this();
        PostToIo([self, id, rsp]() { self->SendResponseMsg(id, rsp); });
    }

    /**
     * @brief Send one response message to the session which issued the request.
     * @param[in] id Id of the request.
     * @param[in] msg Response message object, the request id is filled in.
     */
    void SendResponseMsg(uint64_t id, std::shared_ptr<nlohmann::json> msg)
    {
        IncomingRequest::Ptr req;
        {
            std::lock_guard<std::mutex> lock(request_map_mutex);
            const auto                  it = request_map.find(id);
            if (it != request_map.end())
            {
                req = it->second;
                request_map.erase(it);
            }
        }

        if (req)
        {
            RemoteSession::Ptr session;
            {
                std::lock_guard<std::mutex> lock(session_map_mutex);
                const auto                  it = session_map.find(req->session_id);
                if (it != session_map.end())
                {
                    session = it->second.session;
                }
            }

            if (session)
            {
                (*msg)["id"] = req->id;
                session->Send(std::make_shared<std::string>(msg->dump()));
            }
            else
            {
                SPDLOG_ERROR("session id {} not found", req->session_id);
            }
        }
        else
        {
            SPDLOG_ERROR("SendResponse(id={}): not found", id);
        }

        /*
         * The request counts as answered when its answer is queued for the
         * client, also when its session is gone. A stop which observes no
         * in-flight request may therefore drain the sessions without losing an
         * answer which was produced but not handed to the transport.
         */
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (inflight > 0)
            {
                --inflight;
            }
        }
        state_cv.notify_all();
    }

    /**
     * @brief Query the process at the other end of the session of a request.
     * @param[in] request_id Id of the request.
     * @return Process id of the client, zero when it is unknown.
     */
    DWORD PeerProcessIdOfRequest(uint64_t request_id) const
    {
        uint64_t session_id = 0;
        {
            std::lock_guard<std::mutex> lock(request_map_mutex);
            const auto                  it = request_map.find(request_id);
            if (it == request_map.end())
            {
                return 0;
            }
            session_id = it->second->session_id;
        }

        std::lock_guard<std::mutex> lock(session_map_mutex);
        const auto                  it = session_map.find(session_id);
        if (it == session_map.end())
        {
            return 0;
        }

        return it->second.peer_pid;
    }

    /**
     * @brief Start the IO thread and the accept loop.
     * @return true when the server is listening, otherwise false.
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
         * The thread does not log when it ends: the server may be destroyed by
         * a static destructor of the process, which runs after the logger of
         * the process is gone.
         */
        thread = std::thread([context]() { context->run(); });

        return true;
    }

    /**
     * @brief Stop accepting connections and end the IO thread.
     *
     * The call is idempotent. The pending requests are answered first, then the
     * sessions are released and the IO thread ends.
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

        /* Wait for the requests which are already being handled. */
        {
            std::unique_lock<std::mutex> lock(state_mutex);
            state_cv.wait_for(lock, std::chrono::milliseconds(kInflightTimeoutMs), [this] { return inflight == 0; });
            if (inflight != 0)
            {
                SPDLOG_WARN("{} request(s) are still unanswered", inflight);
            }
        }

        /* Let the answers reach the clients, then release the sessions. */
        SessionMap sessions;
        {
            std::lock_guard<std::mutex> lock(session_map_mutex);
            sessions.swap(session_map);
        }
        for (auto& item : sessions)
        {
            item.second.session->WaitForPendingWrites(kSessionDrainTimeoutMs);
            item.second.session->Close();
        }
        sessions.clear();

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

        {
            std::lock_guard<std::mutex> lock(io_post_mutex);
            io_context.reset();
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex);
            state = Lifecycle::Stopped;
        }
        state_cv.notify_all();
    }

    /**
     * @brief Register a method callback.
     * @param[in] method Method name.
     * @param[in] callback Callback of the method.
     */
    void RegisterMethod(const std::string& method, MethodCallback callback)
    {
        std::lock_guard<std::mutex> lock(method_map_mutex);
        method_callbacks[method] = std::move(callback);
    }

    /* Delays of the retries of a transient accept failure, in milliseconds. */
    static constexpr std::array<uint32_t, 3> kAcceptRetryDelaysMs = { 50, 100, 200 };

    /* Upper bound of the wait for the pending requests of a stop, in milliseconds. */
    static constexpr uint32_t kInflightTimeoutMs = 5000;

    /* Upper bound of the wait for the answers of one session, in milliseconds. */
    static constexpr uint32_t kSessionDrainTimeoutMs = 2000;

    std::string       pipe_path;        /* Name of the pipe */
    Options           options;          /* Options of the server */
    MethodCallbackMap method_callbacks; /* Registered methods */

    detail::RemoteServerSecurityDescriptor security;           /* DACL and label of the pipe */
    detail::RemoteServerSecurityDescriptor security_dacl_only; /* DACL of the pipe without the label */

    std::shared_ptr<asio::io_context> io_context; /* IO context of the private IO thread */

    std::thread          thread;  /* Working thread */
    std::atomic_uint64_t uid_gen; /* Request id generator */

    std::shared_ptr<asio::windows::stream_handle> listener;               /* Instance which waits for a client */
    bool                                          first_instance = false; /* First creation of the pipe */
    std::mutex                                    listener_mutex;         /* Listener lock */

    IncomingRequestMap request_map;       /* Incoming request map */
    mutable std::mutex request_map_mutex; /* Incoming request map lock */

    SessionMap         session_map;       /* Session map */
    mutable std::mutex session_map_mutex; /* Session map lock */

    std::mutex method_map_mutex; /* Method callback map lock */

    AcceptFailureCallback accept_failure_callback; /* Fatal accept failure callback */
    std::mutex            callback_mutex;          /* Callback lock */

    Lifecycle               state = Lifecycle::Running; /* Lifecycle of the server */
    mutable std::mutex      state_mutex;                /* State and in-flight lock */
    std::condition_variable state_cv;                   /* Signals a state change */
    std::size_t             inflight = 0;               /* Requests which are not answered */
    std::size_t             accept_retry = 0;           /* Retries of the accept loop */
    std::atomic_bool        stopping = false;           /* Stop was requested */
    std::atomic_bool        accept_failed = false;      /* The accept loop is dead */

    std::mutex io_post_mutex; /* Guards the IO context against the shutdown */
};

/* Definition of the contract on the declaration. */
inline RemoteServer::RemoteServer() : data_(std::make_shared<Data>())
{
}

/* Definition of the contract on the declaration. */
inline RemoteServer::~RemoteServer()
{
    if (data_)
    {
        data_->Stop();
    }
    data_.reset();
}

/* Definition of the contract on the declaration. */
inline RemoteServer::Ptr RemoteServer::Create(const std::string& pipe)
{
    Options options;
    options.first_instance = false;

    CreateResult result = CreateCompetitive(pipe, options);
    if (result.status != ServerCreateStatus::Created || !result.server)
    {
        throw std::system_error(static_cast<int>(result.error_code), std::system_category(),
                                "CreateNamedPipe() failed");
    }

    return result.server;
}

/* Definition of the contract on the declaration. */
inline RemoteServer::CreateResult RemoteServer::CreateCompetitive(const std::string& pipe, const Options& options)
{
    CreateResult result;

    Ptr obj(new RemoteServer);
    obj->data_->pipe_path = pipe;
    obj->data_->options = options;
    obj->data_->first_instance = options.first_instance;

    if (options.restrict_to_current_user && (!obj->data_->security.Build(detail::kRemoteServerPipeSddl) ||
                                             !obj->data_->security_dacl_only.Build(detail::kRemoteServerPipeDacl)))
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
        const bool peer_reachable = (error == ERROR_ACCESS_DENIED) && detail::RemoteServerPeerIsReachable(pipe);

        result.status = detail::ClassifyRemoteServerCreateError(error, peer_reachable);
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
    result.server = obj;
    return result;
}

/* Definition of the contract on the declaration. */
inline void RemoteServer::RegisterMethod(const std::string& method, MethodCallback callback)
{
    data_->RegisterMethod(method, std::move(callback));
}

/* Definition of the contract on the declaration. */
inline bool RemoteServer::Start()
{
    return data_->Start();
}

/* Definition of the contract on the declaration. */
inline void RemoteServer::Stop()
{
    if (data_)
    {
        data_->Stop();
    }
}

/* Definition of the contract on the declaration. */
inline void RemoteServer::SetAcceptFailureCallback(AcceptFailureCallback callback)
{
    std::lock_guard<std::mutex> lock(data_->callback_mutex);
    data_->accept_failure_callback = std::move(callback);
}

/* Definition of the contract on the declaration. */
inline void RemoteServer::SendResponse(uint64_t id, const RemoteResult& result)
{
    if (result.has_value())
    {
        data_->SendResponse(id, result.value());
        return;
    }

    auto err = result.error();
    data_->SendError(id, err.code, err.message, err.data);
}

/* Definition of the contract on the declaration. */
inline DWORD RemoteServer::PeerProcessIdOfRequest(uint64_t request_id) const
{
    return data_->PeerProcessIdOfRequest(request_id);
}

} // namespace appbox

#endif
