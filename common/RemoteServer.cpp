#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
#include <map>
#include <utility>
#include <thread>
#include "RemoteSession.hpp"
#include "RemoteServer.hpp"

typedef std::map<std::string, appbox::RemoteServer::MethodCallback> MethodCallbackMap;
typedef std::map<uint64_t, appbox::RemoteSession::Ptr>              SessionMap;

struct IncomingRequest
{
    typedef std::shared_ptr<IncomingRequest> Ptr;

    IncomingRequest(uint64_t id, uint64_t session_id) : uid(id), session_id(session_id)
    {
    }

    const uint64_t uid;        /* Global unique id */
    const uint64_t session_id; /* Session id. */
    nlohmann::json request;    /* Client request */
    std::string    method;     /* Request method. */
    nlohmann::json id;         /* Request id in request object. */
};
typedef std::map<uint64_t, IncomingRequest::Ptr> IncomingRequestMap;

struct appbox::RemoteServer::Data : std::enable_shared_from_this<Data>
{
    Data();
    ~Data();
    void Thread();
    void AcceptNext();
    void OnConnected(asio::error_code ec, std::shared_ptr<asio::windows::stream_handle> pipe);
    void OnDataReceived(uint64_t session_id, RemoteSession::MsgPtr data);
    void SendResponse(uint64_t id, const nlohmann::json& result);
    void SendError(uint64_t id, int errcode, const std::string& err_msg,
                   const nlohmann::json& err_data = nullptr);

    WeakPtr           owner;
    std::string       pipe_path;
    MethodCallbackMap method_callbacks;
    asio::io_context  io_context;

    std::thread*         thread;  /* Working thread */
    std::atomic_uint64_t uid_gen; /* Request id generator */

    IncomingRequestMap request_map;       /* Incoming request map */
    std::mutex         request_map_mutex; /* Incoming request map lock */

    SessionMap session_map;       /* Session map */
    std::mutex session_map_mutex; /* Session map lock. */
};

appbox::RemoteServer::Data::Data() : thread(nullptr), uid_gen(0)
{
}

appbox::RemoteServer::Data::~Data()
{
    io_context.stop();

    if (thread != nullptr)
    {
        thread->join();
        delete thread;
    }
}

static HANDLE CreatePipeInstance(const std::string& pipe_name)
{
    HANDLE h = ::CreateNamedPipeA(pipe_name.c_str(),
                                  PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, // IOCP
                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                  PIPE_UNLIMITED_INSTANCES, // Allow any number of clients
                                  4096,                     // Output buffer
                                  4096,                     // Input buffer
                                  0,                        // Default timeout
                                  nullptr);
    // Default security descriptor

    if (h == INVALID_HANDLE_VALUE)
    {
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(),
                                "CreateNamedPipe() failed");
    }
    return h;
}

static void SendResponseMsg(appbox::RemoteServer::Data* ctx, uint64_t id,
                            std::shared_ptr<nlohmann::json> msg)
{
    IncomingRequest::Ptr req;
    {
        std::lock_guard<std::mutex> lock(ctx->request_map_mutex);
        auto                        it = ctx->request_map.find(id);
        if (it == ctx->request_map.end())
        {
            SPDLOG_ERROR("SendError(id={}): not found", id);
            return;
        }
        req = it->second;
        ctx->request_map.erase(it);
    }

    appbox::RemoteSession::Ptr session;
    {
        std::lock_guard<std::mutex> guard(ctx->session_map_mutex);
        auto                        it = ctx->session_map.find(req->session_id);
        if (it == ctx->session_map.end())
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

void appbox::RemoteServer::Data::Thread()
{
    io_context.run();
    SPDLOG_DEBUG("Thread exit");
}

void appbox::RemoteServer::Data::AcceptNext()
{
    auto self = shared_from_this();
    auto pipe =
        std::make_shared<asio::windows::stream_handle>(io_context, CreatePipeInstance(pipe_path));
    asio::windows::overlapped_ptr ov(io_context, [self, pipe](asio::error_code ec, std::size_t) {
        self->OnConnected(ec, pipe);
    });

    BOOL  ok = ConnectNamedPipe(pipe->native_handle(), ov.get());
    DWORD err = GetLastError();

    if (ok)
    { /* Finished */
        ov.complete(asio::error_code(), 0);
        return;
    }

    if (err == ERROR_IO_PENDING)
    { /* Process by IOCP */
        ov.release();
        return;
    }

    if (err == ERROR_PIPE_CONNECTED)
    { /* Client connected between CreateNamedPipe() and ConnectNamedPipe() */
        ov.complete(asio::error_code(), 0);
        return;
    }

    /* Error occur */
    ov.complete(asio::error_code(err, std::system_category()), 0);
}

void appbox::RemoteServer::Data::OnConnected(asio::error_code                              ec,
                                           std::shared_ptr<asio::windows::stream_handle> pipe)
{
    if (ec)
    {
        SPDLOG_ERROR("[HANDLE: {}] server accept failed: {}", pipe->native_handle(), ec.message());
        AcceptNext();
        return;
    }

    auto self = shared_from_this();

    uint64_t session_id = uid_gen++;
    SPDLOG_DEBUG("[HANDLE: {}] server accept success: session_id={}", pipe->native_handle(),
                 session_id);

    auto session = RemoteSession::Create(
        std::move(pipe), [self, session_id](const asio::error_code& ec, RemoteSession::MsgPtr data) {
            if (ec)
            {
                SPDLOG_ERROR("[SESSION: {}] session closed: {}", session_id, ec.message());
                self->session_map.erase(session_id);
                return;
            }
            self->OnDataReceived(session_id, std::move(data));
        });

    /* Save session */
    {
        std::lock_guard<std::mutex> lock(session_map_mutex);
        session_map.insert(SessionMap::value_type(session_id, session));
    }

    session->Start();
}

void appbox::RemoteServer::Data::OnDataReceived(uint64_t session_id, RemoteSession::MsgPtr data)
{
    SPDLOG_TRACE("data recv");

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
        SendError(req->uid, -32601, "Method not found");
        return;
    }

    auto fn = it->second;
    fn(req->uid, req->request["params"]);
}

void appbox::RemoteServer::Data::SendResponse(uint64_t id, const nlohmann::json& result)
{
    auto rsp = std::make_shared<nlohmann::json>();
    (*rsp)["jsonrpc"] = "2.0";
    (*rsp)["result"] = result;

    auto self = shared_from_this();
    asio::post(io_context, [self, id, rsp]() { SendResponseMsg(self.get(), id, rsp); });
}

void appbox::RemoteServer::Data::SendError(uint64_t id, int errcode, const std::string& err_msg,
                                         const nlohmann::json& err_data)
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
    asio::post(io_context, [self, id, rsp]() { SendResponseMsg(self.get(), id, rsp); });
}

appbox::RemoteServer::RemoteServer()
{
    data_ = std::make_shared<Data>();
}

appbox::RemoteServer::~RemoteServer()
{
    data_.reset();
}

appbox::RemoteServer::Ptr appbox::RemoteServer::Create(const std::string& pipe)
{
    auto obj = Ptr(new RemoteServer);
    obj->data_->owner = obj;
    obj->data_->pipe_path = pipe;

    return obj;
}

void appbox::RemoteServer::RegisterMethod(const std::string& method_name, MethodCallback callback)
{
    data_->method_callbacks[method_name] = std::move(callback);
}

void appbox::RemoteServer::Start()
{
    if (data_->thread != nullptr)
    {
        throw std::runtime_error("PipeServer is already started");
    }

    data_->AcceptNext();
    data_->thread = new std::thread(&Data::Thread, data_);
}

void appbox::RemoteServer::SendResponse(uint64_t id, const RemoteResult& result)
{
    if (result.has_value())
    {
        data_->SendResponse(id, result.value());
        return;
    }

    auto err = result.error();
    data_->SendError(id, err.code, err.message, err.data);
}
