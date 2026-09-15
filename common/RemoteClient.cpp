#include <spdlog/spdlog.h>
#include <atomic>
#include <utility>
#include <map>
#include <mutex>
#include "RpcCodec.hpp"
#include "RemoteSession.hpp"
#include "RemoteClient.hpp"

/* Number of attempts used to open the pipe of the server. */
constexpr size_t kConnectRetries = 5;

/* Timeout of a single attempt to wait for a busy pipe instance. */
constexpr DWORD kConnectRetryTimeoutMs = 1000;

struct PipeClientRequest
{
    typedef std::shared_ptr<PipeClientRequest> Ptr;

    PipeClientRequest(uint64_t id) : id(id)
    {
    }

    const uint64_t                   id;      /* UID */
    nlohmann::json                   req;     /* Request data. */
    std::promise<appbox::RemoteResult> promise; /* Promise */
};
typedef std::map<uint64_t, PipeClientRequest::Ptr> RequestMap;

struct appbox::RemoteClient::Data : std::enable_shared_from_this<Data>
{
    Data();
    ~Data();

    void ConnectThread();
    void WorkThread();
    void OnRecv(const asio::error_code&, RemoteSession::MsgPtr);

    std::atomic_uint64_t uid_gen;    /* UID generator. */
    RemoteSession::Ptr     session;    /* RPC session. */
    asio::io_context     io_context; /* IO context */
    std::string          pipe_path;  /* Named pipe path. */
    std::thread*         thread;     /* Work thread */

    RequestMap request_map;       /* Request map */
    std::mutex request_map_mutex; /* Request map mutex */
};

appbox::RemoteClient::Data::Data() : uid_gen(0), thread(nullptr)
{
}

appbox::RemoteClient::Data::~Data()
{
    io_context.stop();
    if (thread != nullptr)
    {
        thread->join();
        delete thread;
        thread = nullptr;
    }
}

static std::wstring GetErrorString(DWORD errorMessageID)
{
    if (errorMessageID == 0)
    {
        return L"";
    }

    LPWSTR messageBuffer = nullptr;

    const DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                          FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                      (LPWSTR)&messageBuffer, 0, nullptr);

    if (size == 0 || messageBuffer == nullptr)
    {
        /* No description is available for this error code. */
        return L"";
    }

    std::wstring message(messageBuffer, size);

    LocalFree(messageBuffer);

    return message;
}

void appbox::RemoteClient::Data::ConnectThread()
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (size_t i = 0; i < kConnectRetries; ++i)
    {
        pipe = CreateFileA(pipe_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE)
        {
            break;
        }

        DWORD err = GetLastError();
        if (err != ERROR_PIPE_BUSY)
        {
            SPDLOG_ERROR(L"cannot open pipe: {}({})", GetErrorString(err), err);
            return;
        }

        if (!WaitNamedPipeA(pipe_path.c_str(), kConnectRetryTimeoutMs))
        {
            SPDLOG_ERROR("cannot open pipe");
            return;
        }
    }

    if (pipe == INVALID_HANDLE_VALUE)
    {
        /* Every attempt found the pipe busy, do not continue with no handle. */
        SPDLOG_ERROR("cannot open pipe: every attempt was busy");
        return;
    }

    DWORD mode = PIPE_READMODE_BYTE;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
    {
        SPDLOG_ERROR("SetNamedPipeHandleState() failed: {}", GetLastError());
        CloseHandle(pipe);
        return;
    }

    std::shared_ptr<asio::windows::stream_handle> handle;
    try
    {
        handle = std::make_shared<asio::windows::stream_handle>(io_context, pipe);
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("failed to create the pipe stream: {}", e.what());
        CloseHandle(pipe);
        return;
    }

    auto self = shared_from_this();
    session =
        RemoteSession::Create(handle, [self](const asio::error_code& ec, RemoteSession::MsgPtr msg) {
            self->OnRecv(ec, std::move(msg));
        });
    session->Start();
}

void appbox::RemoteClient::Data::WorkThread()
{
    io_context.run();
    SPDLOG_DEBUG("Thread exit");
}

void appbox::RemoteClient::Data::OnRecv(const asio::error_code& ec, RemoteSession::MsgPtr msg)
{
    if (ec)
    {
        SPDLOG_ERROR("PipeClient recv failed: {}", ec.message());
        return;
    }

    nlohmann::json rsp;
    try
    {
        rsp = nlohmann::json::parse(*msg);
    }
    catch (const nlohmann::json::exception& e)
    {
        SPDLOG_ERROR("failed to parse the response: {}", e.what());
        return;
    }

    uint64_t     id = 0;
    RemoteResult result;
    std::string  error;
    if (!ParseRpcResponse(rsp, id, result, error))
    {
        SPDLOG_ERROR("invalid response: {}", error);
        return;
    }

    PipeClientRequest::Ptr orig_req;
    {
        std::lock_guard<std::mutex> lock(request_map_mutex);
        auto                        it = request_map.find(id);
        if (it == request_map.end())
        {
            SPDLOG_ERROR("request id {} not found", id);
            return;
        }
        orig_req = it->second;
        request_map.erase(it);
    }

    try
    {
        orig_req->promise.set_value(std::move(result));
    }
    catch (const std::future_error& e)
    {
        SPDLOG_ERROR("failed to deliver the response of request {}: {}", id, e.what());
    }
}

appbox::RemoteClient::RemoteClient()
{
    data_ = std::make_shared<Data>();
}

appbox::RemoteClient::~RemoteClient()
{
    data_.reset();
}

appbox::RemoteClient::Ptr appbox::RemoteClient::Create(const std::string& pipe_path)
{
    Ptr obj(new RemoteClient);
    obj->data_->pipe_path = pipe_path;
    return obj;
}

bool appbox::RemoteClient::Start()
{
    if (data_->session.get() != nullptr)
    {
        SPDLOG_ERROR("PipeClient already started");
        return false;
    }

    data_->thread = new std::thread(&Data::ConnectThread, data_);
    data_->thread->join();
    delete data_->thread;
    data_->thread = nullptr;

    if (data_->session.get() == nullptr)
    {
        SPDLOG_ERROR("connect failed");
        return false;
    }

    data_->thread = new std::thread(&Data::WorkThread, data_);
    return true;
}

appbox::PipeResultFuture appbox::RemoteClient::Call(const std::string&    method,
                                                  const nlohmann::json& param)
{
    uint64_t id = data_->uid_gen++;
    auto     req = std::make_shared<PipeClientRequest>(id);

    req->req["jsonrpc"] = "2.0";
    req->req["method"] = method;
    req->req["id"] = id;
    req->req["params"] = param;

    {
        std::lock_guard<std::mutex> lock(data_->request_map_mutex);
        data_->request_map.insert(RequestMap::value_type(id, req));
    }

    auto msg = std::make_shared<std::string>(req->req.dump());

    auto ctx = data_;
    asio::post(data_->io_context, [ctx, msg]() { ctx->session->Send(msg); });

    return req->promise.get_future();
}
