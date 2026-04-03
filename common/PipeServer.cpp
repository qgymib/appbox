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

struct IncomingRequest
{
    typedef std::shared_ptr<IncomingRequest> Ptr;

    IncomingRequest(uint64_t id) : req_uid(id)
    {
    }

    const uint64_t req_uid; /* Global unique id */
    nlohmann::json request; /* Client request */
    std::string    method;  /* Request method. */
    nlohmann::json id;      /* Request id in request object. */
};
typedef std::map<uint64_t, IncomingRequest::Ptr> IncomingRequestMap;

struct appbox::PipeServer::Data : std::enable_shared_from_this<Data>
{
    Data();
    ~Data();
    void Thread();
    void AcceptNext();
    void OnConnected(asio::error_code ec, std::shared_ptr<asio::windows::stream_handle> pipe);
    void OnDataReceived(const std::string& data);
    void SendError(uint64_t id, int errcode, const std::string& error_msg);

    WeakPtr           owner;
    std::wstring      pipe_path;
    MethodCallbackMap method_callbacks;
    asio::io_context  io_context;

    std::thread*         thread;       /* Working thread */
    std::atomic_bool     looping;      /* looping flag */
    std::atomic_uint64_t next_req_uid; /* Request id generator */

    IncomingRequestMap request_map;       /* Incoming request map */
    std::mutex         request_map_mutex; /* Incoming request map lock */
};

appbox::PipeServer::Data::Data() : thread(nullptr), looping(true), next_req_uid(0)
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
        auto session = PipeSession::Create(
            pipe, [self](const std::string& data) { self->OnDataReceived(data); });
        session->Start();
    }
    else
    {
        SPDLOG_ERROR("server accept failed: {}", ec.message());
    }

    AcceptNext();
}

void appbox::PipeServer::Data::OnDataReceived(const std::string& data)
{
    auto req = std::make_shared<IncomingRequest>(next_req_uid++);
    req->request = nlohmann::json::parse(data);
    req->method = req->request["method"].get<std::string>();
    req->id = req->request["id"];

    /* Save to request map */
    {
        std::lock_guard<std::mutex> lock(request_map_mutex);
        request_map.insert(IncomingRequestMap::value_type(req->req_uid, req));
    }

    auto it = method_callbacks.find(req->method);
    if (it == method_callbacks.end())
    {
        SendError(req->req_uid, -32601, "Method not found");
        return;
    }

    auto fn = it->second;
    fn(req->req_uid, req->request);
}

void appbox::PipeServer::Data::SendError(uint64_t id, int errcode, const std::string& error_msg)
{
    {
        std::lock_guard<std::mutex> lock(request_map_mutex);
        auto it = request_map.find(id);
        if (it == request_map.end())
        {
            SPDLOG_ERROR("SendError(id={}): not found", id);
            return;
        }
    }


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
