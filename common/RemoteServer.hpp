#ifndef APPBOX_COMMON_REMOTE_SERVER_HPP
#define APPBOX_COMMON_REMOTE_SERVER_HPP

#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
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
 * @brief JSON-RPC server listening on a named pipe.
 *
 * The server owns a private IO thread which accepts pipe connections and
 * dispatches incoming requests to registered method callbacks.
 */
class RemoteServer : public std::enable_shared_from_this<RemoteServer>
{
public:
    typedef std::function<void(uint64_t, const nlohmann::json&)> MethodCallback;
    typedef std::shared_ptr<RemoteServer>                        Ptr;
    typedef std::weak_ptr<RemoteServer>                          WeakPtr;

    /**
     * @brief Create a server for the given named pipe path.
     * @param[in] pipe Name of the pipe, for example \\.\pipe\appbox.
     * @return The new server instance.
     */
    static Ptr Create(const std::string& pipe);

    virtual ~RemoteServer();

    /**
     * @brief Register a method callback for handling JSON requests.
     * @param[in] method Method name to register.
     * @param[in] callback Callback function to handle the method.
     */
    void RegisterMethod(const std::string& method, MethodCallback callback);

    /**
     * @brief Start the pipe server and begin listening for incoming connections.
     */
    void Start();

    /**
     * @brief Send a JSON response to a client.
     * @param[in] id Request ID.
     * @param[in] result JSON response data or error.
     */
    void SendResponse(uint64_t id, const RemoteResult& result);

private:
    RemoteServer();

    /**
     * @brief Internal state of the server.
     */
    struct Data;

    std::shared_ptr<Data> data_;
};

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

        const uint64_t    uid;        /* Global unique id */
        const uint64_t    session_id; /* Session id */
        nlohmann::json    request;    /* Client request */
        std::string       method;     /* Request method */
        nlohmann::json    id;         /* Request id in the request object */
    };

    typedef std::map<std::string, MethodCallback>   MethodCallbackMap;
    typedef std::map<uint64_t, RemoteSession::Ptr>  SessionMap;
    typedef std::map<uint64_t, IncomingRequest::Ptr> IncomingRequestMap;

    Data()
        : uid_gen(0)
    {
    }

    ~Data()
    {
        io_context.stop();
        if (thread.joinable())
        {
            thread.join();
        }
    }

    /**
     * @brief Entry point of the IO thread.
     */
    void Thread()
    {
        io_context.run();
        SPDLOG_DEBUG("Thread exit");
    }

    /**
     * @brief Create one named pipe instance and wait for a client to connect.
     */
    void AcceptNext()
    {
        auto self = shared_from_this();
        auto pipe = std::make_shared<asio::windows::stream_handle>(io_context, CreatePipeInstance(pipe_path));
        asio::windows::overlapped_ptr ov(io_context,
                                         [self, pipe](asio::error_code ec, std::size_t) { self->OnConnected(ec, pipe); });

        BOOL  ok = ConnectNamedPipe(pipe->native_handle(), ov.get());
        DWORD err = GetLastError();

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
            SPDLOG_ERROR("[HANDLE: {}] server accept failed: {}", pipe->native_handle(), ec.message());
            AcceptNext();
            return;
        }

        auto self = shared_from_this();

        uint64_t session_id = uid_gen++;
        SPDLOG_DEBUG("[HANDLE: {}] server accept success: session_id={}", pipe->native_handle(), session_id);

        auto session = RemoteSession::Create(
            std::move(pipe), [self, session_id](const asio::error_code& ec, RemoteSession::MsgPtr data) {
                if (ec)
                {
                    SPDLOG_DEBUG("[SESSION: {}] session closed: {}", session_id, ec.message());
                    {
                        std::lock_guard<std::mutex> guard(self->session_map_mutex);
                        self->session_map.erase(session_id);
                    }
                    return;
                }
                self->OnDataReceived(session_id, std::move(data));
            });

        /* Save session */
        {
            std::lock_guard<std::mutex> lock(session_map_mutex);
            session_map.insert(SessionMap::value_type(session_id, session));
        }

        AcceptNext();
        session->Start();
    }

    /**
     * @brief Parse one received message and dispatch it to the registered callback.
     * @param[in] session_id Id of the session which received the message.
     * @param[in] data Message payload.
     */
    void OnDataReceived(uint64_t session_id, RemoteSession::MsgPtr data)
    {
        auto req = std::make_shared<IncomingRequest>(uid_gen++, session_id);
        req->request = nlohmann::json::parse(*data);
        req->method = req->request["method"].get<std::string>();
        req->id = req->request["id"];

        /* Save to request map */
        {
            std::lock_guard<std::mutex> lock(request_map_mutex);
            request_map.insert(IncomingRequestMap::value_type(req->uid, req));
        }

        auto it = method_callbacks.find(req->method);
        if (it == method_callbacks.end())
        {
            SPDLOG_ERROR("method not found: {}", req->method);
            SendError(req->uid, -32601, "Method not found");
            return;
        }

        auto fn = it->second;
        fn(req->uid, req->request["params"]);
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
        asio::post(io_context, [self, id, rsp]() { self->SendResponseMsg(id, rsp); });
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
        asio::post(io_context, [self, id, rsp]() { self->SendResponseMsg(id, rsp); });
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
            auto                        it = request_map.find(id);
            if (it == request_map.end())
            {
                SPDLOG_ERROR("SendResponse(id={}): not found", id);
                return;
            }
            req = it->second;
            request_map.erase(it);
        }

        RemoteSession::Ptr session;
        {
            std::lock_guard<std::mutex> guard(session_map_mutex);
            auto                        it = session_map.find(req->session_id);
            if (it == session_map.end())
            {
                SPDLOG_ERROR("session id {} not found", req->session_id);
                return;
            }
            session = it->second;
        }

        (*msg)["id"] = req->id;

        auto data = std::make_shared<std::string>(msg->dump());
        session->Send(data);
    }

    /**
     * @brief Create one named pipe instance.
     * @param[in] pipe_name Full path of the pipe.
     * @return The pipe handle.
     * @throw std::system_error The pipe instance cannot be created.
     */
    static HANDLE CreatePipeInstance(const std::string& pipe_name)
    {
        HANDLE h = ::CreateNamedPipeA(pipe_name.c_str(),
                                      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, /* IOCP */
                                      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                      PIPE_UNLIMITED_INSTANCES, /* Allow any number of clients */
                                      4096,                     /* Output buffer */
                                      4096,                     /* Input buffer */
                                      0,                        /* Default timeout */
                                      nullptr);                 /* Default security descriptor */

        if (h == INVALID_HANDLE_VALUE)
        {
            throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(),
                                    "CreateNamedPipe() failed");
        }
        return h;
    }

    std::string       pipe_path;        /* Name of the pipe */
    MethodCallbackMap method_callbacks; /* Registered methods */

    asio::io_context io_context; /* IO context of the private IO thread */

    std::thread         thread;  /* Working thread */
    std::atomic_uint64_t uid_gen; /* Request id generator */

    IncomingRequestMap request_map;       /* Incoming request map */
    std::mutex         request_map_mutex; /* Incoming request map lock */

    SessionMap session_map;       /* Session map */
    std::mutex session_map_mutex; /* Session map lock */
};

inline RemoteServer::RemoteServer()
    : data_(std::make_shared<Data>())
{
}

inline RemoteServer::~RemoteServer()
{
    data_.reset();
}

inline RemoteServer::Ptr RemoteServer::Create(const std::string& pipe)
{
    Ptr obj(new RemoteServer);
    obj->data_->pipe_path = pipe;

    return obj;
}

inline void RemoteServer::RegisterMethod(const std::string& method, MethodCallback callback)
{
    data_->method_callbacks[method] = std::move(callback);
}

inline void RemoteServer::Start()
{
    if (data_->thread.joinable())
    {
        throw std::runtime_error("RemoteServer is already started");
    }

    data_->AcceptNext();
    data_->thread = std::thread(&Data::Thread, data_);
}

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

} // namespace appbox

#endif // APPBOX_COMMON_REMOTE_SERVER_HPP
