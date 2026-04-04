#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
#include <map>
#include <utility>
#include <thread>
#include "PipeSession.hpp"
#include "PipeServer.hpp"

typedef std::map<std::string, appbox::PipeServer::MethodCallback> MethodCallbackMap;
typedef std::map<uint64_t, appbox::PipeSession::Ptr>              SessionMap;

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

struct appbox::PipeServer::Data : std::enable_shared_from_this<Data>
{
    Data();
    ~Data();
    void Thread();
    void AcceptNext();
    void OnConnected(asio::error_code ec, std::shared_ptr<asio::windows::stream_handle> pipe);
    void OnDataReceived(uint64_t session_id, PipeSession::MsgPtr data);
    void SendResponse(uint64_t id, const nlohmann::json& result);
    void SendError(uint64_t id, int errcode, const std::string& err_msg,
                   const nlohmann::json& err_data = nullptr);

    WeakPtr           owner;
    std::wstring      pipe_path;
    MethodCallbackMap method_callbacks;
    asio::io_context  io_context;

    std::thread*         thread;  /* Working thread */
    std::atomic_bool     looping; /* looping flag */
    std::atomic_uint64_t uid_gen; /* Request id generator */

    IncomingRequestMap request_map;       /* Incoming request map */
    std::mutex         request_map_mutex; /* Incoming request map lock */

    SessionMap session_map;       /* Session map */
    std::mutex session_map_mutex; /* Session map lock. */
};

appbox::PipeServer::Data::Data() : thread(nullptr), looping(true), uid_gen(0)
{
}

appbox::PipeServer::Data::~Data()
{
    looping = false;
    io_context.stop();

    if (thread != nullptr)
    {
        thread->join();
        delete thread;
    }
}

static HANDLE CreatePipeInstance(const std::wstring& pipe_name)
{
    HANDLE h = ::CreateNamedPipeW(pipe_name.c_str(),
                                  PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, // 双向 + 重叠IO
                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                  PIPE_UNLIMITED_INSTANCES, // ← 允许任意数量客户端
                                  4096,                     // 输出缓冲区
                                  4096,                     // 输入缓冲区
                                  0,                        // 默认超时
                                  nullptr);                 // 默认安全描述符

    if (h == INVALID_HANDLE_VALUE)
    {
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(),
                                "CreateNamedPipe() failed");
    }
    return h;
}

static void SendResponseMsg(appbox::PipeServer::Data* ctx, uint64_t id, nlohmann::json& msg)
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

    appbox::PipeSession::Ptr session;
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

    msg["id"] = req->id;

    auto data = std::make_shared<std::string>(msg.dump());
    session->Send(data);
}

void appbox::PipeServer::Data::Thread()
{
    AcceptNext();
    while (looping)
    {
        io_context.run();
    }
}

void appbox::PipeServer::Data::AcceptNext()
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

void appbox::PipeServer::Data::OnConnected(asio::error_code                              ec,
                                           std::shared_ptr<asio::windows::stream_handle> pipe)
{
    auto self = shared_from_this();
    if (!ec)
    {
        uint64_t session_id = uid_gen++;
        auto     session =
            PipeSession::Create(std::move(pipe), [self, session_id](const asio::error_code& ec,
                                                                    PipeSession::MsgPtr     data) {
                if (ec)
                {
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
    else
    {
        SPDLOG_ERROR("server accept failed: {}", ec.message());
    }

    AcceptNext();
}

void appbox::PipeServer::Data::OnDataReceived(uint64_t session_id, PipeSession::MsgPtr data)
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
        SendError(req->uid, -32601, "Method not found");
        return;
    }

    auto fn = it->second;
    fn(req->uid, req->request);
}

void appbox::PipeServer::Data::SendResponse(uint64_t id, const nlohmann::json& result)
{
    nlohmann::json rsp;
    rsp["jsonrpc"] = "2.0";
    rsp["result"] = result;

    SendResponseMsg(this, id, rsp);
}

void appbox::PipeServer::Data::SendError(uint64_t id, int errcode, const std::string& err_msg,
                                         const nlohmann::json& err_data)
{
    nlohmann::json err;
    err["code"] = errcode;
    err["message"] = err_msg;
    if (!err_data.is_null())
    {
        err["data"] = err_data;
    }

    nlohmann::json rsp;
    rsp["jsonrpc"] = "2.0";
    rsp["error"] = err;

    SendResponseMsg(this, id, rsp);
}

appbox::PipeServer::PipeServer()
{
    data_ = std::make_shared<Data>();
}

appbox::PipeServer::~PipeServer()
{
    data_.reset();
}

appbox::PipeServer::Ptr appbox::PipeServer::Create(const std::wstring& pipe)
{
    auto obj = Ptr(new PipeServer);
    obj->data_->owner = obj;
    obj->data_->pipe_path = pipe;

    return obj;
}

void appbox::PipeServer::RegisterMethod(const std::string& method_name, MethodCallback callback)
{
    data_->method_callbacks[method_name] = std::move(callback);
}

void appbox::PipeServer::Start()
{
    if (data_->thread != nullptr)
    {
        throw std::runtime_error("PipeServer is already started");
    }

    data_->thread = new std::thread(&Data::Thread, data_);
}

void appbox::PipeServer::SendResponse(uint64_t id, const nlohmann::json& result)
{
    data_->SendResponse(id, result);
}

void appbox::PipeServer::SendError(uint64_t id, int errcode, const std::string& err_msg,
                                   const nlohmann::json& err_data)
{
    data_->SendError(id, errcode, err_msg, err_data);
}
